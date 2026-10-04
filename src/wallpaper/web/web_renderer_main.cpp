// Optional out-of-process QtWebEngine renderer for web wallpapers. Renders the
// page with the best available backend and publishes BGRA frames into a
// shared-memory buffer owned by the engine process.

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>

#include <QApplication>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <memory>

#include "wallpaper/web/web_ipc.h"
#include "wallpaper/web/web_render_control.h"
#include "wallpaper/web/web_widget_backend.h"

namespace {

const char* argValue(int argc, char** argv, const char* name, const char* fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], name) == 0) return argv[i + 1];
    }
    return fallback;
}

// The offscreen QPA plugin forces Qt Quick's software adaptation, so the
// render-control backend needs a real session platform. Without a display the
// widget backend runs under offscreen instead.
bool hasSessionDisplay() {
    const char* wayland = getenv("WAYLAND_DISPLAY");
    const char* x11 = getenv("DISPLAY");
    return (wayland && wayland[0]) || (x11 && x11[0]);
}

}  // namespace

int main(int argc, char** argv) {
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    const bool has_display = hasSessionDisplay();
    if (!has_display) setenv("QT_QPA_PLATFORM", "offscreen", 1);
    setenv("QTWEBENGINE_DISABLE_SANDBOX", "1", 0);
    if (!getenv("QTWEBENGINE_CHROMIUM_FLAGS")) {
        setenv("QTWEBENGINE_CHROMIUM_FLAGS",
               "--no-sandbox --disable-dev-shm-usage --ignore-gpu-blocklist --enable-gpu-rasterization "
               "--use-gl=angle --use-angle=gl",
               1);
    }
    // Must be set before QApplication.
    const char* devtools_port = argValue(argc, argv, "--devtools", nullptr);
    if (devtools_port && devtools_port[0]) setenv("QTWEBENGINE_REMOTE_DEBUGGING", devtools_port, 1);

    const char* html = argValue(argc, argv, "--html", nullptr);
    const char* properties = argValue(argc, argv, "--properties", "{}");
    const int memfd = atoi(argValue(argc, argv, "--memfd", "-1"));
    const int ctrl_fd = atoi(argValue(argc, argv, "--ctrl", "-1"));
    const uint32_t width = static_cast<uint32_t>(atoi(argValue(argc, argv, "--width", "1280")));
    const uint32_t height = static_cast<uint32_t>(atoi(argValue(argc, argv, "--height", "720")));
    const uint32_t fps = static_cast<uint32_t>(atoi(argValue(argc, argv, "--fps", "60")));
    if (!html || memfd < 0 || width == 0 || height == 0) {
        fprintf(stderr, "web renderer: missing arguments\n");
        return 2;
    }

    const size_t size = sizeof(WebFrameBuffer) + static_cast<size_t>(width) * height * 4;
    void* mapping = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
    if (mapping == MAP_FAILED) {
        fprintf(stderr, "web renderer: mmap failed\n");
        return 3;
    }
    auto* frame = reinterpret_cast<WebFrameBuffer*>(mapping);
    frame->width = width;
    frame->height = height;
    frame->pixel_format = 1;

    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QtWebEngineQuick::initialize();
    QApplication app(argc, argv);

    const bool widget_only = getenv("LWE_WEB_WIDGET_ONLY") != nullptr;
    std::unique_ptr<web_renderer::FrameRenderer> renderer;
    if (has_display && !widget_only) {
        auto candidate = std::make_unique<web_renderer::RenderControlBackend>(frame, width, height, fps);
        if (candidate->start(html, properties, ctrl_fd)) {
            fprintf(stderr, "web renderer: using offscreen render-control backend\n");
            renderer = std::move(candidate);
        } else {
            fprintf(stderr, "web renderer: render-control backend unavailable, falling back\n");
        }
    }
    if (!renderer) {
        auto candidate = std::make_unique<web_renderer::WidgetBackend>(frame, width, height, fps);
        if (!candidate->start(html, properties, ctrl_fd)) {
            fprintf(stderr, "web renderer: widget backend failed\n");
            return 4;
        }
        fprintf(stderr, "web renderer: using widget backend\n");
        renderer = std::move(candidate);
    }
    return app.exec();
}
