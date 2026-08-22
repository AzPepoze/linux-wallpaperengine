from pathlib import Path


def replace(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise RuntimeError(f"expected text not found in {path}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


def write(path: str, content: str) -> None:
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content)


write(
    "src/formats/wallpaper_engine/texture/video_frame.h",
    r'''#ifndef WALLPAPER_ENGINE_VIDEO_FRAME_H
#define WALLPAPER_ENGINE_VIDEO_FRAME_H

#include <cstdint>

namespace wallpaper_engine {

enum class VideoFrameFormat {
    Unknown,
    NV12,
    RGBA8,
    DmaDrm,
};

enum class VideoFrameMemory {
    VulkanImage,
    DmaBuf,
    Cpu,
};

struct VideoFrameContract {
    VideoFrameFormat format = VideoFrameFormat::Unknown;
    uint32_t width = 0;
    uint32_t height = 0;
    int64_t timestamp_ns = -1;
    int64_t duration_ns = 0;
    VideoFrameMemory memory = VideoFrameMemory::Cpu;
    uintptr_t image_handle = 0;
    int dma_buf_fd = -1;
    uintptr_t synchronization_handle = 0;
};

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_VIDEO_FRAME_H
''',
)

write(
    "src/formats/wallpaper_engine/texture/video_texture.h",
    r'''#ifndef WALLPAPER_ENGINE_VIDEO_TEXTURE_H
#define WALLPAPER_ENGINE_VIDEO_TEXTURE_H

#include <cstdint>
#include <memory>
#include <string>

#include "sokol_gfx.h"
#include "video_frame.h"

namespace wallpaper_engine {

enum class VideoBackendMode {
    Vulkan,
    VaApi,
    Software,
};

class VideoTexture {
   public:
    ~VideoTexture();
    VideoTexture(const VideoTexture&) = delete;
    VideoTexture& operator=(const VideoTexture&) = delete;

    static std::unique_ptr<VideoTexture> open(const char* texture_path);

    uint32_t width() const;
    uint32_t height() const;
    float frameDuration() const;
    VideoBackendMode mode() const;
    const char* modeName() const;
    const std::string& fallbackReason() const;
    const VideoFrameContract& lastFrame() const;

    // The output remains a normal RGBA Sokol image so every existing image
    // effect and shader can keep using the same renderer-facing contract.
    bool attachOutput(sg_image output_image);
    bool update(float elapsed_seconds);

   private:
    VideoTexture() = default;
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace wallpaper_engine

#endif  // WALLPAPER_ENGINE_VIDEO_TEXTURE_H
''',
)

write(
    "src/render/backend/sokol/sokol_vulkan_interop.h",
    r'''#ifndef SOKOL_VULKAN_INTEROP_H
#define SOKOL_VULKAN_INTEROP_H

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "sokol_gfx.h"

struct SokolVulkanInteropContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphics_queue = VK_NULL_HANDLE;
    uint32_t graphics_queue_family = VK_QUEUE_FAMILY_IGNORED;
    VkQueue video_decode_queue = VK_NULL_HANDLE;
    uint32_t video_decode_queue_family = VK_QUEUE_FAMILY_IGNORED;
    const VkPhysicalDeviceFeatures2* enabled_features = nullptr;
    std::vector<const char*> enabled_extensions;
    bool h264_video_decode_enabled = false;
};

struct SokolVulkanImageInfo {
    VkImage image = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t queue_family = VK_QUEUE_FAMILY_IGNORED;
    int width = 0;
    int height = 0;
};

bool sokol_vulkan_get_interop_context(SokolVulkanInteropContext* out_context);
bool sokol_vulkan_get_image_info(sg_image image, SokolVulkanImageInfo* out_info);
void sokol_vulkan_mark_image_texture_read(sg_image image);

#endif  // SOKOL_VULKAN_INTEROP_H
''',
)

write(
    "src/formats/wallpaper_engine/texture/video_texture.cpp",
    r'''#include "video_texture.h"

#include <gst/allocators/gstdmabuf.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/video/video-info-dma-drm.h>
#include <gst/video/video.h>
#include <gst/vulkan/vulkan.h>
#include <libplacebo/gpu.h>
#include <libplacebo/log.h>
#include <libplacebo/renderer.h>
#include <libplacebo/vulkan.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/logger.h"
#include "render/backend/sokol/sokol_vulkan_interop.h"

#define TAG "VIDEO"

namespace wallpaper_engine {
namespace {

constexpr size_t kAppSrcChunkSize = 256 * 1024;
constexpr GstClockTime kPrerollTimeout = 5 * GST_SECOND;

uint32_t readBe32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

bool readEmbeddedMp4(const char* path, std::vector<uint8_t>& mp4) {
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    const long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size <= 12) {
        fclose(file);
        return false;
    }

    std::vector<uint8_t> bytes((size_t)file_size);
    const bool read_ok = fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    fclose(file);
    if (!read_ok) return false;

    for (size_t i = 4; i + 4 <= bytes.size(); ++i) {
        if (memcmp(bytes.data() + i, "ftyp", 4) != 0) continue;
        const size_t box_start = i - 4;
        const uint32_t box_size = readBe32(bytes.data() + box_start);
        if (box_size < 8 || box_start + box_size > bytes.size()) continue;
        mp4.assign(bytes.begin() + (ptrdiff_t)box_start, bytes.end());
        return true;
    }
    return false;
}

bool factoryAvailable(const char* name) {
    GstElementFactory* factory = gst_element_factory_find(name);
    if (!factory) return false;
    gst_object_unref(factory);
    return true;
}

const char* backendName(VideoBackendMode mode) {
    switch (mode) {
        case VideoBackendMode::Vulkan:
            return "Vulkan";
        case VideoBackendMode::VaApi:
            return "VA-API";
        case VideoBackendMode::Software:
            return "software";
    }
    return "unknown";
}

void initGStreamerOnce() {
    static std::once_flag once;
    std::call_once(once, [] { gst_init(nullptr, nullptr); });
}

class PlaceboBridge {
   public:
    ~PlaceboBridge() { shutdown(); }

    bool init(const SokolVulkanInteropContext& context, std::string& reason) {
        shutdown();
        context_ = context;

        pl_log_params log_params = {};
        log_params.log_cb = pl_log_color;
        log_params.log_level = PL_LOG_WARN;
        log_ = pl_log_create(PL_API_VER, &log_params);
        if (!log_) {
            reason = "libplacebo log initialization failed";
            return false;
        }

        pl_vulkan_import_params params = {};
        params.instance = context_.instance;
        params.get_proc_addr = vkGetInstanceProcAddr;
        params.phys_device = context_.physical_device;
        params.device = context_.device;
        params.extensions = context_.enabled_extensions.empty() ? nullptr : context_.enabled_extensions.data();
        params.num_extensions = (int)context_.enabled_extensions.size();
        params.queue_graphics.index = context_.graphics_queue_family;
        params.queue_graphics.count = 1;
        params.queue_compute = params.queue_graphics;
        params.queue_transfer = params.queue_graphics;
        params.features = context_.enabled_features;

        vulkan_ = pl_vulkan_import(log_, &params);
        if (!vulkan_) {
            reason = "libplacebo could not import Sokol's Vulkan device";
            shutdown();
            return false;
        }
        renderer_ = pl_renderer_create(log_, vulkan_->gpu);
        if (!renderer_) {
            reason = "libplacebo renderer creation failed";
            shutdown();
            return false;
        }
        return true;
    }

    bool renderVulkan(GstBuffer* buffer, uint32_t width, uint32_t height, sg_image output, GstVulkanQueue* decode_queue,
                      VideoFrameContract& contract, std::string& reason) {
        if (!renderer_ || !vulkan_ || !buffer || !decode_queue) return false;

        gst_vulkan_queue_submit_lock(decode_queue);
        const VkResult wait_result = vkQueueWaitIdle(decode_queue->queue);
        gst_vulkan_queue_submit_unlock(decode_queue);
        if (wait_result != VK_SUCCESS) {
            reason = "waiting for the Vulkan decoder queue failed";
            return false;
        }

        struct WrappedInput {
            pl_tex texture = nullptr;
            GstVulkanImageMemory* memory = nullptr;
            VkImageLayout original_layout = VK_IMAGE_LAYOUT_UNDEFINED;
            uint32_t original_qf = VK_QUEUE_FAMILY_IGNORED;
        };
        std::vector<WrappedInput> wrapped_inputs;
        std::vector<pl_plane> source_planes;

        const guint memory_count = gst_buffer_n_memory(buffer);
        for (guint memory_index = 0; memory_index < memory_count; ++memory_index) {
            GstMemory* memory = gst_buffer_peek_memory(buffer, memory_index);
            if (!gst_is_vulkan_image_memory(memory)) {
                reason = "vulkanh264dec produced a non-Vulkan memory block";
                cleanupInputs(wrapped_inputs);
                return false;
            }

            auto* image_memory = reinterpret_cast<GstVulkanImageMemory*>(memory);
            if (image_memory->device->device != context_.device) {
                reason = "GStreamer Vulkan decoder did not use Sokol's VkDevice";
                cleanupInputs(wrapped_inputs);
                return false;
            }

            GstVulkanBarrierImageInfo barrier = {};
            gst_vulkan_image_memory_lock(image_memory);
            gst_vulkan_image_memory_peek_barrier_unlocked(image_memory, &barrier);
            gst_vulkan_image_memory_unlock(image_memory);

            pl_vulkan_wrap_params wrap = {};
            wrap.image = image_memory->image;
            wrap.width = (int)gst_vulkan_image_memory_get_width(image_memory);
            wrap.height = (int)gst_vulkan_image_memory_get_height(image_memory);
            wrap.depth = 0;
            wrap.format = image_memory->create_info.format;
            wrap.usage = image_memory->usage;
            pl_tex texture = pl_vulkan_wrap(vulkan_->gpu, &wrap);
            if (!texture) {
                reason = "libplacebo could not wrap a decoded Vulkan image";
                cleanupInputs(wrapped_inputs);
                return false;
            }

            uint32_t queue_family = decode_queue->family;
            if (barrier.parent.queue) queue_family = barrier.parent.queue->family;
            pl_vulkan_release_params release = {};
            release.tex = texture;
            release.layout = barrier.image_layout;
            release.qf = queue_family;
            pl_vulkan_release_ex(vulkan_->gpu, &release);

            wrapped_inputs.push_back({texture, image_memory, barrier.image_layout, queue_family});

            if (texture->params.format && texture->params.format->num_planes > 0) {
                for (int plane = 0; plane < texture->params.format->num_planes; ++plane) {
                    pl_plane p = {};
                    p.texture = texture->planes[plane];
                    source_planes.push_back(p);
                }
            } else {
                pl_plane p = {};
                p.texture = texture;
                source_planes.push_back(p);
            }
        }

        if (source_planes.size() < 2) {
            reason = "decoded NV12 Vulkan image does not expose two sampleable planes";
            returnInputs(wrapped_inputs);
            cleanupInputs(wrapped_inputs);
            return false;
        }
        assignNv12Components(source_planes);

        SokolVulkanImageInfo output_info = {};
        if (!sokol_vulkan_get_image_info(output, &output_info)) {
            reason = "could not expose the Sokol video output image";
            returnInputs(wrapped_inputs);
            cleanupInputs(wrapped_inputs);
            return false;
        }

        pl_vulkan_wrap_params output_wrap = {};
        output_wrap.image = output_info.image;
        output_wrap.width = output_info.width;
        output_wrap.height = output_info.height;
        output_wrap.depth = 0;
        output_wrap.format = output_info.format;
        output_wrap.usage = output_info.usage;
        pl_tex output_texture = pl_vulkan_wrap(vulkan_->gpu, &output_wrap);
        if (!output_texture) {
            reason = "libplacebo could not wrap the Sokol RGBA video image";
            returnInputs(wrapped_inputs);
            cleanupInputs(wrapped_inputs);
            return false;
        }

        pl_vulkan_release_params output_release = {};
        output_release.tex = output_texture;
        output_release.layout = output_info.layout;
        output_release.qf = output_info.queue_family;
        pl_vulkan_release_ex(vulkan_->gpu, &output_release);

        pl_frame source = {};
        source.num_planes = std::min((int)source_planes.size(), PL_MAX_PLANES);
        for (int i = 0; i < source.num_planes; ++i) source.planes[i] = source_planes[(size_t)i];
        source.repr = pl_color_repr_hdtv;
        source.color = pl_color_space_bt709;
        source.crop = {0.0f, 0.0f, (float)width, (float)height};

        pl_frame target = {};
        target.num_planes = 1;
        target.planes[0].texture = output_texture;
        target.planes[0].components = 4;
        for (int i = 0; i < 4; ++i) target.planes[0].component_mapping[i] = i;
        target.repr = pl_color_repr_srgb;
        target.color = pl_color_space_srgb;
        target.crop = {0.0f, 0.0f, (float)width, (float)height};

        const bool rendered = pl_render_image(renderer_, &source, &target, &pl_render_fast_params);

        std::vector<VkSemaphore> semaphores;
        holdTexture(output_texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, context_.graphics_queue_family, semaphores);
        for (const WrappedInput& input : wrapped_inputs)
            holdTexture(input.texture, input.original_layout, input.original_qf, semaphores);
        pl_gpu_finish(vulkan_->gpu);
        for (VkSemaphore semaphore : semaphores) pl_vulkan_sem_destroy(vulkan_->gpu, &semaphore);

        pl_tex_destroy(vulkan_->gpu, &output_texture);
        cleanupInputs(wrapped_inputs);
        if (!rendered) {
            reason = "libplacebo NV12 to RGBA conversion failed";
            return false;
        }

        sokol_vulkan_mark_image_texture_read(output);
        contract.format = VideoFrameFormat::NV12;
        contract.memory = VideoFrameMemory::VulkanImage;
        contract.image_handle = memory_count > 0
                                    ? (uintptr_t)reinterpret_cast<GstVulkanImageMemory*>(gst_buffer_peek_memory(buffer, 0))->image
                                    : 0;
        contract.synchronization_handle = (uintptr_t)decode_queue->queue;
        return true;
    }

    bool renderDmaBuf(GstBuffer* buffer, GstCaps* caps, uint32_t width, uint32_t height, sg_image output,
                      VideoFrameContract& contract, std::string& reason) {
        if (!renderer_ || !vulkan_ || !buffer || !caps) return false;
        if ((vulkan_->gpu->import_caps.tex & PL_HANDLE_DMA_BUF) == 0) {
            reason = "libplacebo/Vulkan driver cannot import DMA-BUF textures";
            return false;
        }

        GstVideoInfoDmaDrm dma_info = {};
        gst_video_info_dma_drm_init(&dma_info);
        if (!gst_video_info_dma_drm_from_caps(&dma_info, caps)) {
            reason = "VA-API output is missing DMA_DRM metadata";
            return false;
        }

        if (gst_buffer_n_memory(buffer) != 1) {
            reason = "multi-fd VA-API DMA-BUF frames are not supported by the bridge";
            return false;
        }
        GstMemory* memory = gst_buffer_peek_memory(buffer, 0);
        if (!gst_is_dmabuf_memory(memory)) {
            reason = "VA-API decoder did not negotiate DMA-BUF memory";
            return false;
        }

        const int source_fd = gst_dmabuf_memory_get_fd(memory);
        const int import_fd = dup(source_fd);
        if (import_fd < 0) {
            reason = "failed to duplicate VA-API DMA-BUF fd";
            return false;
        }

        pl_fmt format = pl_find_fourcc(vulkan_->gpu, dma_info.drm_fourcc);
        if (!format || (dma_info.drm_modifier != DRM_FORMAT_MOD_INVALID &&
                        !pl_fmt_has_modifier(format, dma_info.drm_modifier))) {
            close(import_fd);
            reason = "DMA-BUF format/modifier is not importable by libplacebo";
            return false;
        }

        pl_tex_params input_params = {};
        input_params.w = (int)width;
        input_params.h = (int)height;
        input_params.format = format;
        input_params.sampleable = true;
        input_params.import_handle = PL_HANDLE_DMA_BUF;
        input_params.shared_mem.handle.fd = import_fd;
        input_params.shared_mem.drm_format_mod = dma_info.drm_modifier;
        pl_tex input_texture = pl_tex_create(vulkan_->gpu, &input_params);
        if (!input_texture) {
            close(import_fd);
            reason = "libplacebo failed to import the VA-API DMA-BUF";
            return false;
        }

        std::vector<pl_plane> source_planes;
        if (input_texture->params.format && input_texture->params.format->num_planes > 0) {
            for (int plane = 0; plane < input_texture->params.format->num_planes; ++plane) {
                pl_plane p = {};
                p.texture = input_texture->planes[plane];
                source_planes.push_back(p);
            }
        } else {
            pl_plane p = {};
            p.texture = input_texture;
            source_planes.push_back(p);
        }
        if (source_planes.size() < 2) {
            pl_tex_destroy(vulkan_->gpu, &input_texture);
            reason = "imported VA-API NV12 texture does not expose two planes";
            return false;
        }
        assignNv12Components(source_planes);

        SokolVulkanImageInfo output_info = {};
        if (!sokol_vulkan_get_image_info(output, &output_info)) {
            pl_tex_destroy(vulkan_->gpu, &input_texture);
            reason = "could not expose the Sokol video output image";
            return false;
        }
        pl_vulkan_wrap_params output_wrap = {};
        output_wrap.image = output_info.image;
        output_wrap.width = output_info.width;
        output_wrap.height = output_info.height;
        output_wrap.format = output_info.format;
        output_wrap.usage = output_info.usage;
        pl_tex output_texture = pl_vulkan_wrap(vulkan_->gpu, &output_wrap);
        if (!output_texture) {
            pl_tex_destroy(vulkan_->gpu, &input_texture);
            reason = "libplacebo could not wrap the Sokol RGBA video image";
            return false;
        }
        pl_vulkan_release_params output_release = {};
        output_release.tex = output_texture;
        output_release.layout = output_info.layout;
        output_release.qf = output_info.queue_family;
        pl_vulkan_release_ex(vulkan_->gpu, &output_release);

        pl_frame source = {};
        source.num_planes = std::min((int)source_planes.size(), PL_MAX_PLANES);
        for (int i = 0; i < source.num_planes; ++i) source.planes[i] = source_planes[(size_t)i];
        source.repr = pl_color_repr_hdtv;
        source.color = pl_color_space_bt709;
        source.crop = {0.0f, 0.0f, (float)width, (float)height};

        pl_frame target = {};
        target.num_planes = 1;
        target.planes[0].texture = output_texture;
        target.planes[0].components = 4;
        for (int i = 0; i < 4; ++i) target.planes[0].component_mapping[i] = i;
        target.repr = pl_color_repr_srgb;
        target.color = pl_color_space_srgb;
        target.crop = {0.0f, 0.0f, (float)width, (float)height};

        const bool rendered = pl_render_image(renderer_, &source, &target, &pl_render_fast_params);
        std::vector<VkSemaphore> semaphores;
        holdTexture(output_texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, context_.graphics_queue_family, semaphores);
        pl_gpu_finish(vulkan_->gpu);
        for (VkSemaphore semaphore : semaphores) pl_vulkan_sem_destroy(vulkan_->gpu, &semaphore);
        pl_tex_destroy(vulkan_->gpu, &output_texture);
        pl_tex_destroy(vulkan_->gpu, &input_texture);

        if (!rendered) {
            reason = "libplacebo DMA-BUF NV12 to RGBA conversion failed";
            return false;
        }
        sokol_vulkan_mark_image_texture_read(output);
        contract.format = VideoFrameFormat::DmaDrm;
        contract.memory = VideoFrameMemory::DmaBuf;
        contract.dma_buf_fd = source_fd;
        return true;
    }

   private:
    static void assignNv12Components(std::vector<pl_plane>& planes) {
        planes[0].components = 1;
        planes[0].component_mapping[0] = 0;
        planes[1].components = 2;
        planes[1].component_mapping[0] = 1;
        planes[1].component_mapping[1] = 2;
    }

    void holdTexture(pl_tex texture, VkImageLayout layout, uint32_t qf, std::vector<VkSemaphore>& semaphores) {
        VkSemaphore semaphore = pl_vulkan_sem_create(vulkan_->gpu, nullptr);
        if (!semaphore) return;
        pl_vulkan_hold_params hold = {};
        hold.tex = texture;
        hold.layout = layout;
        hold.qf = qf;
        hold.semaphore.sem = semaphore;
        if (pl_vulkan_hold_ex(vulkan_->gpu, &hold))
            semaphores.push_back(semaphore);
        else
            pl_vulkan_sem_destroy(vulkan_->gpu, &semaphore);
    }

    void returnInputs(std::vector<struct WrappedInputPlaceholder>&) {}

    template <typename T>
    void returnInputs(std::vector<T>& inputs) {
        std::vector<VkSemaphore> semaphores;
        for (const T& input : inputs) holdTexture(input.texture, input.original_layout, input.original_qf, semaphores);
        pl_gpu_finish(vulkan_->gpu);
        for (VkSemaphore semaphore : semaphores) pl_vulkan_sem_destroy(vulkan_->gpu, &semaphore);
    }

    template <typename T>
    void cleanupInputs(std::vector<T>& inputs) {
        for (T& input : inputs) {
            if (input.texture) pl_tex_destroy(vulkan_->gpu, &input.texture);
        }
        inputs.clear();
    }

    void shutdown() {
        if (renderer_) pl_renderer_destroy(&renderer_);
        if (vulkan_) pl_vulkan_destroy(&vulkan_);
        if (log_) pl_log_destroy(&log_);
        context_ = {};
    }

    SokolVulkanInteropContext context_;
    pl_log log_ = nullptr;
    pl_vulkan vulkan_ = nullptr;
    pl_renderer renderer_ = nullptr;
};

}  // namespace

struct VideoTexture::Impl {
    std::string path;
    std::vector<uint8_t> media_bytes;
    size_t source_offset = 0;
    bool source_eos = false;

    GstElement* pipeline = nullptr;
    GstElement* source = nullptr;
    GstElement* sink = nullptr;
    GstVulkanInstance* gst_instance = nullptr;
    GstVulkanPhysicalDevice* gst_physical = nullptr;
    GstVulkanDevice* gst_device = nullptr;
    GstVulkanQueue* gst_decode_queue = nullptr;
    GstSample* pending_sample = nullptr;

    SokolVulkanInteropContext sokol_vulkan = {};
    PlaceboBridge bridge;
    VideoBackendMode active_mode = VideoBackendMode::Software;
    std::string fallback_reason;
    uint32_t video_width = 0;
    uint32_t video_height = 0;
    float frame_duration = 1.0f / 30.0f;
    sg_image output_image = {SG_INVALID_ID};
    VideoFrameContract frame_contract;

    ~Impl() { teardownPipeline(); }

    static void onNeedData(GstAppSrc* app_source, guint requested, gpointer user_data) {
        auto* self = static_cast<Impl*>(user_data);
        if (!self || self->source_eos) return;
        if (self->source_offset >= self->media_bytes.size()) {
            self->source_eos = true;
            gst_app_src_end_of_stream(app_source);
            return;
        }

        const size_t desired = requested > 0 ? (size_t)requested : kAppSrcChunkSize;
        const size_t count = std::min({desired, kAppSrcChunkSize, self->media_bytes.size() - self->source_offset});
        GstBuffer* buffer = gst_buffer_new_allocate(nullptr, count, nullptr);
        if (!buffer) return;
        gst_buffer_fill(buffer, 0, self->media_bytes.data() + self->source_offset, count);
        GST_BUFFER_OFFSET(buffer) = self->source_offset;
        GST_BUFFER_OFFSET_END(buffer) = self->source_offset + count;
        self->source_offset += count;
        if (gst_app_src_push_buffer(app_source, buffer) != GST_FLOW_OK) self->source_eos = true;
    }

    static gboolean onSeekData(GstAppSrc*, guint64 offset, gpointer user_data) {
        auto* self = static_cast<Impl*>(user_data);
        if (!self || offset > self->media_bytes.size()) return FALSE;
        self->source_offset = (size_t)offset;
        self->source_eos = false;
        return TRUE;
    }

    bool setupBorrowedVulkan(std::string& reason) {
        if (!sokol_vulkan.h264_video_decode_enabled || sokol_vulkan.instance == VK_NULL_HANDLE ||
            sokol_vulkan.device == VK_NULL_HANDLE) {
            reason = "Sokol Vulkan device was created without VK_KHR_video_decode_h264";
            return false;
        }

        guint32 physical_count = 0;
        if (vkEnumeratePhysicalDevices(sokol_vulkan.instance, &physical_count, nullptr) != VK_SUCCESS ||
            physical_count == 0) {
            reason = "could not enumerate Sokol Vulkan physical devices";
            return false;
        }
        std::vector<VkPhysicalDevice> physical_devices(physical_count);
        if (vkEnumeratePhysicalDevices(sokol_vulkan.instance, &physical_count, physical_devices.data()) != VK_SUCCESS) {
            reason = "could not read Sokol Vulkan physical device list";
            return false;
        }
        guint32 selected_index = physical_count;
        for (guint32 i = 0; i < physical_count; ++i) {
            if (physical_devices[i] == sokol_vulkan.physical_device) {
                selected_index = i;
                break;
            }
        }
        if (selected_index == physical_count) {
            reason = "Sokol physical device is not part of its Vulkan instance";
            return false;
        }

        gst_instance = gst_vulkan_instance_new();
        if (!gst_instance) {
            reason = "could not allocate GStreamer Vulkan instance wrapper";
            return false;
        }
        gst_instance->instance = sokol_vulkan.instance;
        gst_instance->n_physical_device = physical_count;
        gst_instance->physical_devices = g_new(VkPhysicalDevice, physical_count);
        memcpy(gst_instance->physical_devices, physical_devices.data(), sizeof(VkPhysicalDevice) * physical_count);

        gst_physical = gst_vulkan_physical_device_new(gst_instance, selected_index);
        gst_device = gst_physical ? gst_vulkan_device_new(gst_physical) : nullptr;
        if (!gst_device) {
            reason = "could not allocate GStreamer Vulkan device wrapper";
            return false;
        }

        GError* error = nullptr;
        if (!gst_vulkan_device_open(gst_device, &error)) {
            reason = error ? error->message : "GStreamer Vulkan queue discovery failed";
            g_clear_error(&error);
            return false;
        }

        VkDevice temporary_device = gst_device->device;
        gst_device->device = sokol_vulkan.device;
        if (temporary_device != VK_NULL_HANDLE) vkDestroyDevice(temporary_device, nullptr);

        gst_decode_queue = gst_vulkan_device_get_queue(gst_device, sokol_vulkan.video_decode_queue_family, 0);
        if (!gst_decode_queue) {
            reason = "GStreamer could not borrow the Sokol Vulkan video-decode queue";
            return false;
        }
        return true;
    }

    void applyVulkanContexts() {
        if (!pipeline || !gst_instance || !gst_device || !gst_decode_queue) return;
        GstContext* instance_context = gst_context_new(GST_VULKAN_INSTANCE_CONTEXT_TYPE_STR, TRUE);
        gst_context_set_vulkan_instance(instance_context, gst_instance);
        gst_element_set_context(pipeline, instance_context);
        gst_context_unref(instance_context);

        GstContext* device_context = gst_context_new(GST_VULKAN_DEVICE_CONTEXT_TYPE_STR, TRUE);
        gst_context_set_vulkan_device(device_context, gst_device);
        gst_element_set_context(pipeline, device_context);
        gst_context_unref(device_context);

        GstContext* queue_context = gst_context_new(GST_VULKAN_QUEUE_CONTEXT_TYPE_STR, TRUE);
        gst_context_set_vulkan_queue(queue_context, gst_decode_queue);
        gst_element_set_context(pipeline, queue_context);
        gst_context_unref(queue_context);
    }

    bool startPipeline(VideoBackendMode mode, std::string& reason) {
        teardownPipeline();
        source_offset = 0;
        source_eos = false;
        active_mode = mode;

        if (mode == VideoBackendMode::Vulkan) {
            if (!factoryAvailable("vulkanh264dec")) {
                reason = "GStreamer vulkanh264dec plugin is unavailable";
                return false;
            }
            if (!setupBorrowedVulkan(reason)) return false;
            if (!bridge.init(sokol_vulkan, reason)) return false;
        } else if (mode == VideoBackendMode::VaApi) {
            if (!factoryAvailable("vah264dec") || !factoryAvailable("vapostproc")) {
                reason = "GStreamer VA-API H.264 decoder/post-process plugins are unavailable";
                return false;
            }
            if (!bridge.init(sokol_vulkan, reason)) return false;
        }

        const char* description = nullptr;
        switch (mode) {
            case VideoBackendMode::Vulkan:
                description =
                    "appsrc name=source stream-type=seekable format=bytes ! qtdemux name=demux "
                    "demux. ! queue ! h264parse ! vulkanh264dec ! "
                    "video/x-raw(memory:VulkanImage),format=NV12 ! "
                    "appsink name=sink sync=true max-buffers=3 drop=true";
                break;
            case VideoBackendMode::VaApi:
                description =
                    "appsrc name=source stream-type=seekable format=bytes ! qtdemux name=demux "
                    "demux. ! queue ! h264parse ! vah264dec ! vapostproc ! "
                    "video/x-raw(memory:DMABuf),format=DMA_DRM ! "
                    "appsink name=sink sync=true max-buffers=3 drop=true";
                break;
            case VideoBackendMode::Software:
                description =
                    "appsrc name=source stream-type=seekable format=bytes ! qtdemux name=demux "
                    "demux. ! queue ! decodebin ! videoconvert ! video/x-raw,format=RGBA ! "
                    "appsink name=sink sync=true max-buffers=3 drop=true";
                break;
        }

        GError* error = nullptr;
        pipeline = gst_parse_launch(description, &error);
        if (!pipeline) {
            reason = error ? error->message : "GStreamer pipeline creation failed";
            g_clear_error(&error);
            teardownPipeline();
            return false;
        }
        source = gst_bin_get_by_name(GST_BIN(pipeline), "source");
        sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
        if (!source || !sink) {
            reason = "GStreamer pipeline is missing appsrc/appsink";
            teardownPipeline();
            return false;
        }

        GstAppSrcCallbacks callbacks = {};
        callbacks.need_data = onNeedData;
        callbacks.seek_data = onSeekData;
        gst_app_src_set_callbacks(GST_APP_SRC(source), &callbacks, this, nullptr);
        gst_app_src_set_size(GST_APP_SRC(source), (gint64)media_bytes.size());
        gst_app_src_set_stream_type(GST_APP_SRC(source), GST_APP_STREAM_TYPE_SEEKABLE);
        gst_app_src_set_latency(GST_APP_SRC(source), 0, GST_CLOCK_TIME_NONE);
        if (mode == VideoBackendMode::Vulkan) applyVulkanContexts();

        const GstStateChangeReturn paused = gst_element_set_state(pipeline, GST_STATE_PAUSED);
        if (paused == GST_STATE_CHANGE_FAILURE) {
            reason = "GStreamer pipeline could not enter PAUSED state";
            teardownPipeline();
            return false;
        }
        pending_sample = gst_app_sink_try_pull_preroll(GST_APP_SINK(sink), kPrerollTimeout);
        if (!pending_sample) {
            readBusError(reason);
            if (reason.empty()) reason = "GStreamer pipeline produced no preroll video frame";
            teardownPipeline();
            return false;
        }
        updateVideoInfo(pending_sample);
        if (video_width == 0 || video_height == 0) {
            reason = "GStreamer video caps contain invalid dimensions";
            teardownPipeline();
            return false;
        }
        if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            reason = "GStreamer pipeline could not enter PLAYING state";
            teardownPipeline();
            return false;
        }
        LOG_TAG_I(TAG, "Video texture '%s': active mode %s (%ux%u, %.2f FPS)", path.c_str(), backendName(mode),
                  video_width, video_height, frame_duration > 0.0f ? 1.0f / frame_duration : 0.0f);
        return true;
    }

    bool restartAtBeginning() {
        if (!pipeline) return false;
        if (gst_element_seek_simple(pipeline, GST_FORMAT_TIME,
                                    (GstSeekFlags)(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), 0))
            return true;
        source_offset = 0;
        source_eos = false;
        gst_element_set_state(pipeline, GST_STATE_READY);
        return gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE;
    }

    void updateVideoInfo(GstSample* sample) {
        GstCaps* caps = sample ? gst_sample_get_caps(sample) : nullptr;
        if (!caps) return;
        if (active_mode == VideoBackendMode::VaApi) {
            GstVideoInfoDmaDrm dma_info = {};
            gst_video_info_dma_drm_init(&dma_info);
            if (gst_video_info_dma_drm_from_caps(&dma_info, caps)) {
                video_width = GST_VIDEO_INFO_WIDTH(&dma_info.vinfo);
                video_height = GST_VIDEO_INFO_HEIGHT(&dma_info.vinfo);
                if (GST_VIDEO_INFO_FPS_N(&dma_info.vinfo) > 0)
                    frame_duration = (float)GST_VIDEO_INFO_FPS_D(&dma_info.vinfo) /
                                     (float)GST_VIDEO_INFO_FPS_N(&dma_info.vinfo);
                return;
            }
        }
        GstVideoInfo info = {};
        if (gst_video_info_from_caps(&info, caps)) {
            video_width = GST_VIDEO_INFO_WIDTH(&info);
            video_height = GST_VIDEO_INFO_HEIGHT(&info);
            if (GST_VIDEO_INFO_FPS_N(&info) > 0)
                frame_duration = (float)GST_VIDEO_INFO_FPS_D(&info) / (float)GST_VIDEO_INFO_FPS_N(&info);
        }
    }

    bool presentSoftware(GstSample* sample, std::string& reason) {
        GstCaps* caps = gst_sample_get_caps(sample);
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        if (!caps || !buffer) return false;

        GstVideoInfo info = {};
        if (!gst_video_info_from_caps(&info, caps)) {
            reason = "could not parse software RGBA video caps";
            return false;
        }
        GstVideoFrame frame = {};
        if (!gst_video_frame_map(&frame, &info, buffer, GST_MAP_READ)) {
            reason = "could not map software RGBA video frame";
            return false;
        }

        const uint8_t* src = GST_VIDEO_FRAME_PLANE_DATA(&frame, 0);
        const int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
        const size_t tight_stride = (size_t)video_width * 4;
        std::vector<uint8_t> packed;
        const uint8_t* upload = src;
        if (stride != (int)tight_stride) {
            packed.resize(tight_stride * video_height);
            for (uint32_t y = 0; y < video_height; ++y)
                memcpy(packed.data() + y * tight_stride, src + (ptrdiff_t)y * stride, tight_stride);
            upload = packed.data();
        }

        sg_image_data data = {};
        data.mip_levels[0] = {upload, tight_stride * video_height};
        sg_update_image(output_image, &data);
        gst_video_frame_unmap(&frame);
        frame_contract.format = VideoFrameFormat::RGBA8;
        frame_contract.memory = VideoFrameMemory::Cpu;
        return true;
    }

    bool presentSample(GstSample* sample, std::string& reason) {
        if (!sample || output_image.id == SG_INVALID_ID) return false;
        updateVideoInfo(sample);
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        if (!buffer) return false;

        frame_contract = {};
        frame_contract.width = video_width;
        frame_contract.height = video_height;
        if (GST_BUFFER_PTS_IS_VALID(buffer)) frame_contract.timestamp_ns = (int64_t)GST_BUFFER_PTS(buffer);
        if (GST_BUFFER_DURATION_IS_VALID(buffer)) frame_contract.duration_ns = (int64_t)GST_BUFFER_DURATION(buffer);

        switch (active_mode) {
            case VideoBackendMode::Vulkan:
                return bridge.renderVulkan(buffer, video_width, video_height, output_image, gst_decode_queue,
                                           frame_contract, reason);
            case VideoBackendMode::VaApi:
                return bridge.renderDmaBuf(buffer, gst_sample_get_caps(sample), video_width, video_height, output_image,
                                           frame_contract, reason);
            case VideoBackendMode::Software:
                return presentSoftware(sample, reason);
        }
        return false;
    }

    bool fallbackToSoftware(const std::string& reason) {
        const std::string old_mode = backendName(active_mode);
        fallback_reason += (fallback_reason.empty() ? "" : "; ") + old_mode + ": " + reason;
        LOG_TAG_W(TAG, "Video texture '%s': %s path unavailable (%s); falling back to software", path.c_str(),
                  old_mode.c_str(), reason.c_str());
        std::string software_reason;
        if (!startPipeline(VideoBackendMode::Software, software_reason)) {
            fallback_reason += "; software: " + software_reason;
            LOG_TAG_E(TAG, "Video texture '%s': software fallback failed: %s", path.c_str(), software_reason.c_str());
            return false;
        }
        return true;
    }

    bool attachOutput(sg_image image) {
        output_image = image;
        if (!pending_sample) return true;
        std::string reason;
        GstSample* first = pending_sample;
        pending_sample = nullptr;
        const bool presented = presentSample(first, reason);
        gst_sample_unref(first);
        if (presented) return true;
        if (active_mode == VideoBackendMode::Software) return false;
        if (!fallbackToSoftware(reason)) return false;
        if (!pending_sample) return true;
        first = pending_sample;
        pending_sample = nullptr;
        reason.clear();
        const bool fallback_presented = presentSample(first, reason);
        gst_sample_unref(first);
        return fallback_presented;
    }

    bool update(float) {
        if (!pipeline || !sink || output_image.id == SG_INVALID_ID) return false;
        processBus();
        GstSample* newest = nullptr;
        for (;;) {
            GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 0);
            if (!sample) break;
            if (newest) gst_sample_unref(newest);
            newest = sample;
        }
        if (!newest) return true;

        std::string reason;
        const bool success = presentSample(newest, reason);
        gst_sample_unref(newest);
        if (success) return true;
        if (active_mode != VideoBackendMode::Software && fallbackToSoftware(reason)) return true;
        LOG_TAG_E(TAG, "Video texture '%s': frame presentation failed in %s mode: %s", path.c_str(),
                  backendName(active_mode), reason.c_str());
        return false;
    }

    void processBus() {
        if (!pipeline) return;
        GstBus* bus = gst_element_get_bus(pipeline);
        if (!bus) return;
        for (;;) {
            GstMessage* message = gst_bus_pop_filtered(bus, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
            if (!message) break;
            if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
                if (!restartAtBeginning()) LOG_TAG_W(TAG, "Video texture '%s': failed to loop stream", path.c_str());
            } else {
                GError* error = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_error(message, &error, &debug);
                LOG_TAG_W(TAG, "Video texture '%s': GStreamer error: %s", path.c_str(),
                          error ? error->message : "unknown error");
                g_clear_error(&error);
                g_free(debug);
            }
            gst_message_unref(message);
        }
        gst_object_unref(bus);
    }

    void readBusError(std::string& reason) {
        if (!pipeline) return;
        GstBus* bus = gst_element_get_bus(pipeline);
        if (!bus) return;
        GstMessage* message = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
        if (message) {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            if (error) reason = error->message;
            g_clear_error(&error);
            g_free(debug);
            gst_message_unref(message);
        }
        gst_object_unref(bus);
    }

    void teardownPipeline() {
        if (pending_sample) {
            gst_sample_unref(pending_sample);
            pending_sample = nullptr;
        }
        if (pipeline) gst_element_set_state(pipeline, GST_STATE_NULL);
        if (sink) {
            gst_object_unref(sink);
            sink = nullptr;
        }
        if (source) {
            gst_object_unref(source);
            source = nullptr;
        }
        if (pipeline) {
            gst_object_unref(pipeline);
            pipeline = nullptr;
        }
        if (gst_decode_queue) {
            gst_object_unref(gst_decode_queue);
            gst_decode_queue = nullptr;
        }
        if (gst_device) {
            // The GstVulkanDevice was deliberately rebound to Sokol's VkDevice.
            // Prevent GStreamer's finalizer from destroying an object it does not own.
            gst_device->device = VK_NULL_HANDLE;
            gst_object_unref(gst_device);
            gst_device = nullptr;
        }
        if (gst_physical) {
            gst_object_unref(gst_physical);
            gst_physical = nullptr;
        }
        if (gst_instance) {
            // Same ownership rule for the borrowed VkInstance.
            gst_instance->instance = VK_NULL_HANDLE;
            gst_object_unref(gst_instance);
            gst_instance = nullptr;
        }
        output_image = {SG_INVALID_ID};
    }
};

VideoTexture::~VideoTexture() = default;

std::unique_ptr<VideoTexture> VideoTexture::open(const char* path) {
    if (!path) return nullptr;
    initGStreamerOnce();

    auto texture = std::unique_ptr<VideoTexture>(new VideoTexture());
    texture->impl = std::make_unique<Impl>();
    texture->impl->path = path;
    if (!readEmbeddedMp4(path, texture->impl->media_bytes)) return nullptr;
    sokol_vulkan_get_interop_context(&texture->impl->sokol_vulkan);

    std::array<VideoBackendMode, 3> candidates = {VideoBackendMode::Vulkan, VideoBackendMode::VaApi,
                                                  VideoBackendMode::Software};
    for (VideoBackendMode candidate : candidates) {
        std::string reason;
        if (texture->impl->startPipeline(candidate, reason)) return texture;
        texture->impl->fallback_reason +=
            (texture->impl->fallback_reason.empty() ? "" : "; ") + std::string(backendName(candidate)) + ": " + reason;
        LOG_TAG_I(TAG, "Video texture '%s': skipping %s path: %s", path, backendName(candidate), reason.c_str());
    }
    LOG_TAG_E(TAG, "Video texture '%s': no usable playback backend (%s)", path, texture->impl->fallback_reason.c_str());
    return nullptr;
}

uint32_t VideoTexture::width() const { return impl ? impl->video_width : 0; }
uint32_t VideoTexture::height() const { return impl ? impl->video_height : 0; }
float VideoTexture::frameDuration() const { return impl ? impl->frame_duration : 0.0f; }
VideoBackendMode VideoTexture::mode() const { return impl ? impl->active_mode : VideoBackendMode::Software; }
const char* VideoTexture::modeName() const { return impl ? backendName(impl->active_mode) : "none"; }
const std::string& VideoTexture::fallbackReason() const {
    static const std::string empty;
    return impl ? impl->fallback_reason : empty;
}
const VideoFrameContract& VideoTexture::lastFrame() const {
    static const VideoFrameContract empty;
    return impl ? impl->frame_contract : empty;
}
bool VideoTexture::attachOutput(sg_image output_image) { return impl && impl->attachOutput(output_image); }
bool VideoTexture::update(float elapsed_seconds) { return impl && impl->update(elapsed_seconds); }

}  // namespace wallpaper_engine
''',
)

# Remove FFmpeg and add pkg-config GStreamer/libplacebo dependencies.
replace(
    "xmake.lua",
    '''add_requires("stb")\nadd_requires("imgui", {optional = true})\n''',
    '''add_requires("stb")\nadd_requires("pkgconfig::gstreamer-1.0", {alias = "gst_core", system = true})\nadd_requires("pkgconfig::gstreamer-app-1.0", {alias = "gst_app", system = true})\nadd_requires("pkgconfig::gstreamer-video-1.0", {alias = "gst_video", system = true})\nadd_requires("pkgconfig::gstreamer-vulkan-1.0", {alias = "gst_vulkan", system = true})\nadd_requires("pkgconfig::gstreamer-allocators-1.0", {alias = "gst_allocators", system = true})\nadd_requires("pkgconfig::libplacebo", {alias = "libplacebo", system = true})\nadd_requires("imgui", {optional = true})\n''',
)
replace(
    "xmake.lua",
    '''    add_packages("sokol", "linmath.h", "slang_shader", "vulkan-headers", "lz4", "cjson", "stb")\n    add_includedirs("src")\n    add_syslinks("vulkan", "X11", "Xcursor", "Xi", "avformat", "avcodec", "avutil", "swscale", "dl", "m", "pthread")\n''',
    '''    add_packages("sokol", "linmath.h", "slang_shader", "vulkan-headers", "lz4", "cjson", "stb",\n                 "gst_core", "gst_app", "gst_video", "gst_vulkan", "gst_allocators", "libplacebo")\n    add_includedirs("src")\n    add_syslinks("vulkan", "X11", "Xcursor", "Xi", "dl", "m", "pthread")\n''',
)

# The output is GPU-renderable for Vulkan/VA-API and stream-updatable only if a
# runtime fallback selects software. Merely enabling stream_update does not
# perform a CPU upload.
replace(
    "src/assets/asset_manager.cpp",
    '''void AssetManager::updateVideoTextures(float elapsed_seconds) {\n    for (ActiveVideoTexture& video : video_textures) {\n        video.elapsed_seconds += elapsed_seconds;\n        if (video.elapsed_seconds < video.decoder->frameDuration()) continue;\n        std::vector<uint8_t> pixels;\n        while (video.elapsed_seconds >= video.decoder->frameDuration()) {\n            if (!video.decoder->decodeNextFrame(pixels)) break;\n            video.elapsed_seconds -= video.decoder->frameDuration();\n        }\n        if (!pixels.empty()) {\n            sg_image_data data = {};\n            data.mip_levels[0] = {pixels.data(), pixels.size()};\n            sg_update_image(video.image, &data);\n        }\n    }\n}\n''',
    '''void AssetManager::updateVideoTextures(float elapsed_seconds) {\n    for (ActiveVideoTexture& video : video_textures) video.decoder->update(elapsed_seconds);\n}\n''',
)
replace(
    "src/assets/asset_manager.cpp",
    '''            std::unique_ptr<wallpaper_engine::VideoTexture> video = wallpaper_engine::VideoTexture::open(abs_path);\n            std::vector<uint8_t> pixels;\n            if (video && video->decodeNextFrame(pixels)) {\n                sg_image_desc desc = {};\n                desc.width = (int)video->width();\n                desc.height = (int)video->height();\n                desc.pixel_format = SG_PIXELFORMAT_RGBA8;\n                desc.usage.stream_update = true;\n                desc.data.mip_levels[0] = {pixels.data(), pixels.size()};\n                const sg_image gpu_image = sg_make_image(&desc);\n                if (gpu_image.id != SG_INVALID_ID) {\n                    video_textures.push_back({abs_path, gpu_image, std::move(video)});\n                    return GfxImage(gpu_image);\n                }\n            }\n''',
    '''            std::unique_ptr<wallpaper_engine::VideoTexture> video = wallpaper_engine::VideoTexture::open(abs_path);\n            if (video) {\n                sg_image_desc desc = {};\n                desc.width = (int)video->width();\n                desc.height = (int)video->height();\n                desc.pixel_format = SG_PIXELFORMAT_RGBA8;\n                desc.usage.color_attachment = true;\n                desc.usage.stream_update = true;\n                const sg_image gpu_image = sg_make_image(&desc);\n                if (gpu_image.id != SG_INVALID_ID && video->attachOutput(gpu_image)) {\n                    LOG_I("Video texture backend: %s (%s)", video->modeName(),\n                          video->fallbackReason().empty() ? "primary path" : video->fallbackReason().c_str());\n                    video_textures.push_back({abs_path, gpu_image, std::move(video)});\n                    return GfxImage(gpu_image);\n                }\n                if (gpu_image.id != SG_INVALID_ID) sg_destroy_image(gpu_image);\n            }\n''',
)
replace(
    "src/assets/asset_manager.h",
    '''        std::unique_ptr<wallpaper_engine::VideoTexture> decoder;\n        float elapsed_seconds = 0.0f;\n''',
    '''        std::unique_ptr<wallpaper_engine::VideoTexture> decoder;\n''',
)

# The 64 MiB staging override is no longer paid by GPU-native playback. Sokol's
# ordinary staging path remains available for the explicit software fallback.
replace(
    "src/app/main.cpp",
    '''    // A single 4K RGBA video frame needs about 32 MiB. Sokol's default Vulkan\n    // streaming staging buffer is 16 MiB, which corrupts per-frame video uploads.\n    s_desc.vulkan.stream_staging_buffer_size = 64 * 1024 * 1024;\n''',
    '',
)

# Disable unsupported audio-driven fullscreen post-process passes before they
# can render their unbound spectrum uniforms as the white/diagonal artifact.
replace(
    "src/wallpaper/scene/2d/effects/effect.cpp",
    '''    bool has_mvp = raw_vs.find("g_ModelViewProjectionMatrix") != std::string::npos;\n    int vertical = combos.count("VERTICAL") ? combos.at("VERTICAL") : 0;\n    is_fullscreen_quad = !render_target.empty() && (!has_mvp || vertical == 0);\n\n    std::string prefix = ShaderSourceProcessor::buildShaderPrefix();\n''',
    '''    bool has_mvp = raw_vs.find("g_ModelViewProjectionMatrix") != std::string::npos;\n    int vertical = combos.count("VERTICAL") ? combos.at("VERTICAL") : 0;\n    is_fullscreen_quad = !render_target.empty() && (!has_mvp || vertical == 0);\n\n    const bool uses_audio_spectrum = raw_vs.find("g_AudioSpectrum") != std::string::npos ||\n                                     raw_fs.find("g_AudioSpectrum") != std::string::npos;\n    const bool fullscreen_post_process = !has_mvp || is_fullscreen_quad;\n    if (fullscreen_post_process && uses_audio_spectrum) {\n        effect_log.warn(\n            "ShaderPass %s: fullscreen effect uses unsupported g_AudioSpectrum* uniforms; preserving the prior "\n            "scene instead of rendering this pass",\n            shader_name.c_str());\n        enabled = false;\n        free(vs_src);\n        free(fs_src);\n        return;\n    }\n\n    std::string prefix = ShaderSourceProcessor::buildShaderPrefix();\n''',
)

# Interpose Sokol's VkDevice creation so the application-owned VkDevice also
# exposes optional Vulkan Video extensions/queues when the driver supports
# H.264 decode. This keeps one VkInstance/VkDevice shared by Sokol, GStreamer,
# and libplacebo instead of creating a second rendering device.
replace(
    "src/render/backend/sokol/sokol_implementation.cpp",
    '''#include "core/build_config.h"\n\n#if !DEBUG_BUILD\n#define SOKOL_NO_ENTRY\n#endif\n''',
    r'''#include "core/build_config.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <string>
#include <vector>

static bool g_lwe_vk_video_h264_enabled = false;
static uint32_t g_lwe_vk_video_queue_family = VK_QUEUE_FAMILY_IGNORED;
static VkQueue g_lwe_vk_video_queue = VK_NULL_HANDLE;
static std::vector<std::string> g_lwe_vk_enabled_extensions;

static bool lwe_vk_has_device_extension(VkPhysicalDevice physical_device, const char* name) {
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr) != VK_SUCCESS) return false;
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, extensions.data()) != VK_SUCCESS)
        return false;
    for (const VkExtensionProperties& extension : extensions) {
        if (strcmp(extension.extensionName, name) == 0) return true;
    }
    return false;
}

static VkResult lwe_vkCreateDevice(VkPhysicalDevice physical_device, const VkDeviceCreateInfo* create_info,
                                   const VkAllocationCallbacks* allocator, VkDevice* device) {
    if (!create_info) return vkCreateDevice(physical_device, create_info, allocator, device);

    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(physical_device, &properties);

    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());

    const bool has_video_extensions = properties.apiVersion >= VK_API_VERSION_1_3 &&
                                      lwe_vk_has_device_extension(physical_device, VK_KHR_VIDEO_QUEUE_EXTENSION_NAME) &&
                                      lwe_vk_has_device_extension(physical_device, VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME) &&
                                      lwe_vk_has_device_extension(physical_device, VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME);

    uint32_t video_family = VK_QUEUE_FAMILY_IGNORED;
    if (has_video_extensions) {
        for (uint32_t i = 0; i < family_count; ++i) {
            if ((families[i].queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) != 0 && families[i].queueCount > 0) {
                video_family = i;
                break;
            }
        }
    }

    std::vector<VkDeviceQueueCreateInfo> queues(create_info->pQueueCreateInfos,
                                                 create_info->pQueueCreateInfos + create_info->queueCreateInfoCount);
    const float queue_priority = 1.0f;
    // GStreamer's Vulkan device wrapper discovers graphics/compute/transfer/video
    // families independently. Make one queue from every family available so its
    // borrowed-device view can safely expose exactly the queues it discovers.
    for (uint32_t family = 0; family < family_count; ++family) {
        if (families[family].queueCount == 0) continue;
        const bool exists = std::any_of(queues.begin(), queues.end(), [family](const VkDeviceQueueCreateInfo& q) {
            return q.queueFamilyIndex == family;
        });
        if (exists) continue;
        VkDeviceQueueCreateInfo queue = {};
        queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex = family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &queue_priority;
        queues.push_back(queue);
    }

    std::vector<const char*> extensions;
    extensions.reserve(create_info->enabledExtensionCount + 3);
    for (uint32_t i = 0; i < create_info->enabledExtensionCount; ++i)
        extensions.push_back(create_info->ppEnabledExtensionNames[i]);
    auto append_extension = [&extensions](const char* extension) {
        if (std::find_if(extensions.begin(), extensions.end(), [extension](const char* existing) {
                return strcmp(existing, extension) == 0;
            }) == extensions.end())
            extensions.push_back(extension);
    };
    if (video_family != VK_QUEUE_FAMILY_IGNORED) {
        append_extension(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);
        append_extension(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
        append_extension(VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME);
    }

    VkDeviceCreateInfo patched = *create_info;
    patched.queueCreateInfoCount = (uint32_t)queues.size();
    patched.pQueueCreateInfos = queues.data();
    patched.enabledExtensionCount = (uint32_t)extensions.size();
    patched.ppEnabledExtensionNames = extensions.data();

    const VkResult result = vkCreateDevice(physical_device, &patched, allocator, device);
    if (result == VK_SUCCESS) {
        g_lwe_vk_video_h264_enabled = video_family != VK_QUEUE_FAMILY_IGNORED;
        g_lwe_vk_video_queue_family = video_family;
        g_lwe_vk_video_queue = VK_NULL_HANDLE;
        if (g_lwe_vk_video_h264_enabled) vkGetDeviceQueue(*device, video_family, 0, &g_lwe_vk_video_queue);
        g_lwe_vk_enabled_extensions.clear();
        g_lwe_vk_enabled_extensions.reserve(extensions.size());
        for (const char* extension : extensions) g_lwe_vk_enabled_extensions.emplace_back(extension);
    }
    return result;
}

#define vkCreateDevice lwe_vkCreateDevice

#if !DEBUG_BUILD
#define SOKOL_NO_ENTRY
#endif
''',
)
replace(
    "src/render/backend/sokol/sokol_implementation.cpp",
    '''#include "sokol_glue.h"\n\n#ifdef LWE_RESTORE_NDEBUG\n''',
    '''#include "sokol_glue.h"\n\n#undef vkCreateDevice\n\n#ifdef LWE_RESTORE_NDEBUG\n''',
)
replace(
    "src/render/backend/sokol/sokol_implementation.cpp",
    '''#if DEBUG_BUILD\n#include "sokol_backend_ext.inl"\n#endif\n''',
    '''#include "sokol_backend_ext.inl"\n''',
)

# Export the small project-owned interop contract from the same TU that owns
# Sokol's Vulkan private implementation. This is compiled in Debug and Release.
with Path("src/render/backend/sokol/sokol_backend_ext.inl").open("a") as f:
    f.write(r'''

#include "sokol_vulkan_interop.h"

bool sokol_vulkan_get_interop_context(SokolVulkanInteropContext* out_context) {
    if (!out_context || !_sg.vk.instance || !_sg.vk.phys_dev || !_sg.vk.dev || !_sg.vk.queue) return false;
    *out_context = {};
    out_context->instance = _sg.vk.instance;
    out_context->physical_device = _sg.vk.phys_dev;
    out_context->device = _sg.vk.dev;
    out_context->graphics_queue = _sg.vk.queue;
    out_context->graphics_queue_family = _sg.vk.queue_family_index;
    out_context->video_decode_queue = g_lwe_vk_video_queue;
    out_context->video_decode_queue_family = g_lwe_vk_video_queue_family;
    out_context->enabled_features = &_sg.vk.dev_features;
    out_context->h264_video_decode_enabled = g_lwe_vk_video_h264_enabled;
    out_context->enabled_extensions.reserve(g_lwe_vk_enabled_extensions.size());
    for (const std::string& extension : g_lwe_vk_enabled_extensions)
        out_context->enabled_extensions.push_back(extension.c_str());
    return true;
}

bool sokol_vulkan_get_image_info(sg_image image, SokolVulkanImageInfo* out_info) {
    if (!out_info || image.id == SG_INVALID_ID) return false;
    _sg_image_t* internal = _sg_lookup_image(image.id);
    if (!internal || !internal->vk.img) return false;
    const sg_image_desc desc = sg_query_image_desc(image);
    if (desc.pixel_format != SG_PIXELFORMAT_RGBA8) return false;

    *out_info = {};
    out_info->image = internal->vk.img;
    out_info->format = VK_FORMAT_R8G8B8A8_UNORM;
    out_info->usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (desc.usage.color_attachment) out_info->usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (desc.usage.storage_attachment) out_info->usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    out_info->layout = _sg_vk_image_layout(internal->vk.cur_access);
    out_info->queue_family = _sg.vk.queue_family_index;
    out_info->width = internal->cmn.width;
    out_info->height = internal->cmn.height;
    return true;
}

void sokol_vulkan_mark_image_texture_read(sg_image image) {
    if (image.id == SG_INVALID_ID) return;
    _sg_image_t* internal = _sg_lookup_image(image.id);
    if (!internal) return;
    internal->vk.cur_access = _SG_VK_ACCESS_TEXTURE;
}
''')

# Runtime/build documentation and plugin requirements.
replace(
    "README.md",
    '''- FFmpeg development libraries (`libavformat`, `libavcodec`, `libavutil`, `libswscale`)\n\nOn Arch / CachyOS:\n\n```bash\nsudo pacman -S xmake vulkan-icd-loader shader-slang libx11 libxcursor libxi ffmpeg\n```\n''',
    '''- GStreamer development libraries (`gstreamer-1.0`, `gstreamer-app-1.0`, `gstreamer-video-1.0`, `gstreamer-vulkan-1.0`, `gstreamer-allocators-1.0`)\n- [libplacebo](https://code.videolan.org/videolan/libplacebo) with Vulkan/DMA-BUF support\n\nOn Arch / CachyOS:\n\n```bash\nsudo pacman -S xmake vulkan-icd-loader shader-slang libx11 libxcursor libxi \\\n  gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-libav libplacebo libva\n```\n\n### GPU-native video texture runtime\n\nEmbedded video textures select the first working backend at runtime:\n\n1. **Vulkan Video H.264** — `vulkanh264dec`, sharing Sokol's Vulkan instance/device and feeding NV12 Vulkan images directly to libplacebo.\n2. **VA-API** — `vah264dec` + `vapostproc`, exporting DMA-BUF frames for Vulkan import.\n3. **Software** — GStreamer decode + `videoconvert` to RGBA, followed by the normal Sokol CPU upload path.\n\nThe GStreamer installation therefore needs MP4 demuxing (`qtdemux`), H.264 parsing (`h264parse`), the Vulkan and VA plugins for hardware paths, and a software decoder such as `gst-libav` for the final fallback. Diagnostics log the selected backend and every fallback reason. Vulkan Video is optional at the driver level.\n''',
)

# Clarify the safe raw-pixel failure: AssetManager will immediately attempt
# embedded-media playback after decodeTexture returns invalid.
replace(
    "src/formats/wallpaper_engine/texture/tex_decoder.cpp",
    '''                          "skipping unsupported payload",\n''',
    '''                          "rejecting raw payload so embedded-media playback can be attempted",\n''',
)

print("GPU-native video patch applied")
