#include "wallpaper/web/web_wallpaper.h"

#include <cjson/cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include "shared/core/build_config.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/backend/surface.h"
#include "shared/graphics/gfx_resource.h"
#include "wallpaper/2d/scene_builder.h"
#include "wallpaper/web/web_dmabuf_ipc.h"
#include "wallpaper/web/web_ipc.h"

extern char** environ;

#if LWE_WEB

namespace {
constexpr const char* kRendererName = "linux-wallpaperengine-webrender";

std::string resolveRendererPath() {
    const char* override_path = getenv("LWE_WEB_RENDERER");
    if (override_path && override_path[0] != '\0') return override_path;

    char exe[1024];
    const ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len <= 0) return kRendererName;
    exe[len] = '\0';
    char* slash = strrchr(exe, '/');
    if (!slash) return kRendererName;
    *slash = '\0';
    return std::string(exe) + "/" + kRendererName;
}

// Default user properties from project.json, in the {name: {value}} shape applyUserProperties expects.
std::string buildUserProperties(const std::string& html_path) {
    std::string dir = html_path;
    const size_t slash = dir.find_last_of('/');
    if (slash == std::string::npos)
        dir = ".";
    else
        dir.resize(slash);

    char* json_str = read_file_to_string((dir + "/project.json").c_str());
    if (!json_str) return "{}";
    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) return "{}";

    std::string result = "{}";
    cJSON* general = cJSON_GetObjectItemCaseSensitive(root, "general");
    cJSON* properties = cJSON_IsObject(general) ? cJSON_GetObjectItemCaseSensitive(general, "properties") : nullptr;
    if (cJSON_IsObject(properties)) {
        cJSON* out = cJSON_CreateObject();
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, properties) {
            cJSON* value = cJSON_GetObjectItemCaseSensitive(item, "value");
            if (!value || !item->string) continue;
            cJSON* entry = cJSON_CreateObject();
            cJSON_AddItemToObject(entry, "value", cJSON_Duplicate(value, 1));
            cJSON_AddItemToObject(out, item->string, entry);
        }
        char* serialized = cJSON_PrintUnformatted(out);
        if (serialized) {
            result = serialized;
            cJSON_free(serialized);
        }
        cJSON_Delete(out);
    }
    cJSON_Delete(root);
    return result;
}
}  // namespace

WebWallpaper::WebWallpaper(EngineContext& ctx) : Scene2DWallpaper(ctx) {}

WebWallpaper::~WebWallpaper() {
    clear();
}

bool WebWallpaper::load(const std::string& path, EngineContext& ctx) {
    clear();

    const uint32_t width = static_cast<uint32_t>(std::max(16, surface::width()));
    const uint32_t height = static_cast<uint32_t>(std::max(16, surface::height()));
    const std::string properties = buildUserProperties(path);

    const size_t size = sizeof(WebFrameBuffer) + static_cast<size_t>(width) * height * 4;
    const int memfd = memfd_create("lwe-web-frame", 0);
    if (memfd < 0) {
        LOG_TAG_E("WEB", "Failed to create frame buffer: %s", strerror(errno));
        return false;
    }
    if (ftruncate(memfd, static_cast<off_t>(size)) != 0) {
        LOG_TAG_E("WEB", "Failed to size frame buffer: %s", strerror(errno));
        close(memfd);
        return false;
    }
    void* mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
    if (mapping == MAP_FAILED) {
        LOG_TAG_E("WEB", "Failed to map frame buffer: %s", strerror(errno));
        close(memfd);
        return false;
    }
    auto* frame = static_cast<WebFrameBuffer*>(mapping);
    frame->reserved = 0;
    frame->frame_counter = 0;
    frame->transport = 0;
    frame->buffer_count = 0;
    memset(frame->buffers, 0, sizeof(frame->buffers));
    new (&frame->published_frame) std::atomic<uint64_t>(0);
    new (&frame->published_index) std::atomic<uint32_t>(0);
    new (&frame->consumed_frame) std::atomic<uint64_t>(0);
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&frame->mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);
    frame->width = width;
    frame->height = height;
    frame->pixel_format = 1;

    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) != 0) {
        LOG_TAG_E("WEB", "Failed to create control socket: %s", strerror(errno));
        pthread_mutex_destroy(&frame->mutex);
        munmap(mapping, size);
        close(memfd);
        return false;
    }

    fcntl(sockets[0], F_SETFD, FD_CLOEXEC);  // only the child's end may leak, or EOF never arrives

    const std::string renderer = resolveRendererPath();
    char memfd_arg[16];
    char ctrl_arg[16];
    char width_arg[16];
    char height_arg[16];
    snprintf(memfd_arg, sizeof(memfd_arg), "%d", memfd);
    snprintf(ctrl_arg, sizeof(ctrl_arg), "%d", sockets[1]);
    snprintf(width_arg, sizeof(width_arg), "%u", width);
    snprintf(height_arg, sizeof(height_arg), "%u", height);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(renderer.c_str()));
    argv.push_back(const_cast<char*>("--html"));
    argv.push_back(const_cast<char*>(path.c_str()));
    argv.push_back(const_cast<char*>("--properties"));
    argv.push_back(const_cast<char*>(properties.c_str()));
    argv.push_back(const_cast<char*>("--memfd"));
    argv.push_back(memfd_arg);
    argv.push_back(const_cast<char*>("--ctrl"));
    argv.push_back(ctrl_arg);
    argv.push_back(const_cast<char*>("--width"));
    argv.push_back(width_arg);
    argv.push_back(const_cast<char*>("--height"));
    argv.push_back(height_arg);
    char fps_arg[16];
    snprintf(fps_arg, sizeof(fps_arg), "%d", std::max(1, ctx.web.render_fps));
    argv.push_back(const_cast<char*>("--fps"));
    argv.push_back(fps_arg);
    argv.push_back(const_cast<char*>("--transport"));
    argv.push_back(const_cast<char*>(webTransportName(ctx.web.transport)));
    char devtools_arg[16] = {};
    if (ctx.web.devtools) {
        snprintf(devtools_arg, sizeof(devtools_arg), "%d", ctx.web.devtools_port);
        argv.push_back(const_cast<char*>("--devtools"));
        argv.push_back(devtools_arg);
    }
    argv.push_back(nullptr);

    pid_t child = -1;
    const int spawn_result = posix_spawn(&child, renderer.c_str(), nullptr, nullptr, argv.data(), environ);
    close(sockets[1]);
    if (spawn_result != 0) {
        LOG_TAG_E("WEB", "Failed to start web renderer '%s': %s", renderer.c_str(), strerror(spawn_result));
        close(sockets[0]);
        pthread_mutex_destroy(&frame->mutex);
        munmap(mapping, size);
        close(memfd);
        return false;
    }

    std::vector<uint8_t> initial(static_cast<size_t>(width) * height * 4, 0);
    sg_image_desc desc = {};
    desc.width = static_cast<int>(width);
    desc.height = static_cast<int>(height);
    desc.pixel_format = SG_PIXELFORMAT_BGRA8;
    desc.usage.color_attachment = true;
    desc.usage.stream_update = true;
    desc.data.mip_levels[0] = {initial.data(), initial.size()};
    const sg_image image = sg_make_image(&desc);
    if (image.id == SG_INVALID_ID) {
        LOG_TAG_E("WEB", "Failed to create frame texture");
        WebInputMessage shutdown = {};
        shutdown.type = WEB_INPUT_SHUTDOWN;
        send(sockets[0], &shutdown, sizeof(shutdown), MSG_NOSIGNAL);
        close(sockets[0]);
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        pthread_mutex_destroy(&frame->mutex);
        munmap(mapping, size);
        close(memfd);
        return false;
    }

    GfxImage gfx(image);
    ParsedScene parsed =
        SceneBuilder::buildImageScene("[WebLayer] Web Wallpaper", std::move(gfx), static_cast<float>(width),
                                      static_cast<float>(height), SCENE_TYPE_WEB, path.c_str(), ctx);
    if (!applyParsedScene(std::move(parsed), ctx)) {
        LOG_TAG_E("WEB", "Failed to build web wallpaper scene: %s", path.c_str());
        close(sockets[0]);
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        pthread_mutex_destroy(&frame->mutex);
        munmap(mapping, size);
        close(memfd);
        return false;
    }

    child_pid_ = child;
    ctrl_fd_ = sockets[0];
    memfd_ = memfd;
    frame_ = frame;
    shm_size_ = size;
    image_ = image;
    width_ = width;
    height_ = height;
    last_frame_counter_ = 0;

    LOG_TAG_I("WEB", "Web wallpaper started (pid %d, %ux%u): %s", child, width, height, path.c_str());
    if (ctx.web.devtools)
        LOG_TAG_I("WEB", "DevTools: open http://localhost:%d in a browser to inspect the page", ctx.web.devtools_port);
    return true;
}

void WebWallpaper::pollChild() {
    if (child_pid_ > 0) {
        int status = 0;
        if (waitpid(child_pid_, &status, WNOHANG) == child_pid_) {
            child_pid_ = -1;
            LOG_TAG_W("WEB", "Web renderer exited (status %d)", status);
        }
    }
    if (!frame_ || image_.id == SG_INVALID_ID) return;

    if (frame_->transport == 1) {
        pollDmaBuf();
        return;
    }

    const int lock_result = pthread_mutex_lock(&frame_->mutex);
    if (lock_result == EOWNERDEAD)
        pthread_mutex_consistent(&frame_->mutex);
    else if (lock_result != 0)
        return;

    if (frame_->frame_counter != last_frame_counter_) {
        last_frame_counter_ = frame_->frame_counter;
        sg_image_data data = {};
        data.mip_levels[0] = {reinterpret_cast<uint8_t*>(frame_) + sizeof(WebFrameBuffer),
                              static_cast<size_t>(frame_->width) * frame_->height * 4};
        sg_update_image(image_, &data);
    }
    pthread_mutex_unlock(&frame_->mutex);
}

void WebWallpaper::pollDmaBuf() {
    if (!dmabuf_offer_received_ && ctrl_fd_ >= 0) {
        int fds[kWebDmaBufBuffers] = {};
        const int count = web_renderer::receiveDmaBufOffer(ctrl_fd_, fds, kWebDmaBufBuffers);
        if (count < 0) {
            dmabuf_offer_received_ = true;
            LOG_TAG_W("WEB", "DMA-BUF offer failed; frame transport unavailable");
            return;
        }
        if (count == 0) return;
        dmabuf_offer_received_ = true;

        if (count != static_cast<int>(kWebDmaBufBuffers) || !gpu_init_zero_copy_bgra()) {
            for (int i = 0; i < count; ++i) close(fds[i]);
            LOG_TAG_W("WEB", "DMA-BUF import unavailable; frame transport disabled");
            return;
        }
        dmabuf_ready_ = true;
        for (int i = 0; i < count; ++i) {
            if (dmabuf_ready_) dmabuf_ready_ = gpu_import_bgra_dmabuf(fds[i], width_, height_, surfaces_[i]);
            close(fds[i]);
        }
        if (!dmabuf_ready_) {
            for (ImportedBgraSurface& surface : surfaces_) gpu_destroy_bgra_surface(surface);
            LOG_TAG_W("WEB", "DMA-BUF import failed; frame transport disabled");
        }
    }
    if (!dmabuf_ready_) return;

    const uint64_t published = frame_->published_frame.load(std::memory_order_acquire);
    if (published == last_published_) return;
    last_published_ = published;

    const uint32_t index = frame_->published_index.load(std::memory_order_acquire);
    if (index < kWebDmaBufBuffers)
        gpu_blit_zero_copy_bgra(surfaces_[index], image_, static_cast<int>(width_), static_cast<int>(height_));
    frame_->consumed_frame.store(published, std::memory_order_release);
}

void WebWallpaper::update(float dt, EngineContext& ctx) {
    pollChild();
    Scene2DWallpaper::update(dt, ctx);
    ctx.web_frame_transport = activeFrameTransport();
}

const char* WebWallpaper::activeFrameTransport() const {
    if (!frame_) return "starting";
    return frame_->transport == 1 ? "dma-buf" : "shared memory";
}

void WebWallpaper::handleInput(const sapp_event* event, EngineContext& ctx) {
    (void)ctx;
    if (ctrl_fd_ < 0 || !event) return;

    WebInputMessage msg = {};
    switch (event->type) {
        case SAPP_EVENTTYPE_MOUSE_MOVE:
            msg.type = WEB_INPUT_MOUSE_MOVE;
            break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:
            msg.type = WEB_INPUT_MOUSE_DOWN;
            msg.button = event->mouse_button;
            break;
        case SAPP_EVENTTYPE_MOUSE_UP:
            msg.type = WEB_INPUT_MOUSE_UP;
            msg.button = event->mouse_button;
            break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            msg.type = WEB_INPUT_MOUSE_SCROLL;
            msg.scroll_x = event->scroll_x;
            msg.scroll_y = event->scroll_y;
            break;
        default:
            return;
    }

    const float window_w = static_cast<float>(std::max(1, surface::width()));
    const float window_h = static_cast<float>(std::max(1, surface::height()));
    msg.x = event->mouse_x / window_w;
    msg.y = event->mouse_y / window_h;
    msg.modifiers = event->modifiers;
    send(ctrl_fd_, &msg, sizeof(msg), MSG_DONTWAIT | MSG_NOSIGNAL);
}

void WebWallpaper::stopChild() {
    if (ctrl_fd_ >= 0) {
        WebInputMessage shutdown = {};
        shutdown.type = WEB_INPUT_SHUTDOWN;
        send(ctrl_fd_, &shutdown, sizeof(shutdown), MSG_NOSIGNAL);
    }
    if (child_pid_ > 0) {
        int status = 0;
        for (int i = 0; i < 20; ++i) {
            if (waitpid(child_pid_, &status, WNOHANG) == child_pid_) {
                child_pid_ = -1;
                break;
            }
            usleep(100000);
        }
        if (child_pid_ > 0) {
            kill(child_pid_, SIGKILL);
            waitpid(child_pid_, &status, 0);
            child_pid_ = -1;
        }
    }
    if (ctrl_fd_ >= 0) {
        close(ctrl_fd_);
        ctrl_fd_ = -1;
    }
}

void WebWallpaper::clear() {
    if (frame_ && dmabuf_ready_) frame_->consumed_frame.store(frame_->published_frame.load(std::memory_order_acquire));
    stopChild();
    for (ImportedBgraSurface& surface : surfaces_) gpu_destroy_bgra_surface(surface);
    dmabuf_offer_received_ = false;
    dmabuf_ready_ = false;
    last_published_ = 0;
    if (frame_) {
        pthread_mutex_destroy(&frame_->mutex);
        munmap(frame_, shm_size_);
        frame_ = nullptr;
        shm_size_ = 0;
    }
    if (memfd_ >= 0) {
        close(memfd_);
        memfd_ = -1;
    }
    image_ = {};
    width_ = 0;
    height_ = 0;
    last_frame_counter_ = 0;
    Scene2DWallpaper::clear();
}

#else  // !LWE_WEB

WebWallpaper::WebWallpaper(EngineContext& ctx) : Scene2DWallpaper(ctx) {}
WebWallpaper::~WebWallpaper() = default;

bool WebWallpaper::load(const std::string&, EngineContext&) {
    return false;
}
void WebWallpaper::update(float, EngineContext&) {}
void WebWallpaper::handleInput(const sapp_event*, EngineContext&) {}
void WebWallpaper::clear() {}

#endif  // LWE_WEB
