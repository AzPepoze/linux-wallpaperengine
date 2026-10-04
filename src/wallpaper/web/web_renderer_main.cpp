// Optional out-of-process QtWebEngine renderer for web wallpapers. Runs with
// the offscreen QPA plugin, grabs the page at a fixed cadence and publishes
// BGRA frames into a shared-memory buffer owned by the engine process.

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPixmap>
#include <QPoint>
#include <QTimer>
#include <QUrl>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineView>
#include <QWheelEvent>
#include <cstdint>

#include "wallpaper/web/web_ipc.h"

namespace {

const char* argValue(int argc, char** argv, const char* name, const char* fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], name) == 0) return argv[i + 1];
    }
    return fallback;
}

Qt::MouseButton toQtButton(uint32_t button) {
    switch (button) {
        case 0:
            return Qt::LeftButton;
        case 1:
            return Qt::RightButton;
        case 2:
            return Qt::MiddleButton;
        default:
            return Qt::NoButton;
    }
}

Qt::KeyboardModifiers toQtModifiers(uint32_t modifiers) {
    Qt::KeyboardModifiers out;
    if (modifiers & (1u << 0)) out |= Qt::ShiftModifier;
    if (modifiers & (1u << 1)) out |= Qt::ControlModifier;
    if (modifiers & (1u << 2)) out |= Qt::AltModifier;
    if (modifiers & (1u << 3)) out |= Qt::MetaModifier;
    return out;
}

// Injected before any page script so wallpapers see the Wallpaper Engine
// browser API. Audio is fed silence for now; a later task replaces the source.
const char* kShimScript = R"JS(
(function () {
    if (window.__lweShimInstalled) return;
    window.__lweShimInstalled = true;

    window.wallpaperPropertyListener = window.wallpaperPropertyListener || {};
    window.__lweAudioCallback = null;
    window.wallpaperRegisterAudioListener = function (cb) {
        window.__lweAudioCallback = (typeof cb === 'function') ? cb : null;
    };
    setInterval(function () {
        if (window.__lweAudioCallback) window.__lweAudioCallback(new Float32Array(128));
    }, 33);

    var noop = function () {};
    window.wallpaperRegisterMediaStatusListener = window.wallpaperRegisterMediaStatusListener || noop;
    window.wallpaperRegisterMediaPropertiesListener = window.wallpaperRegisterMediaPropertiesListener || noop;
    window.wallpaperRegisterMediaThumbnailListener = window.wallpaperRegisterMediaThumbnailListener || noop;
    window.wallpaperRegisterMediaPlaybackListener = window.wallpaperRegisterMediaPlaybackListener || noop;
    window.wallpaperRegisterMediaTimelineListener = window.wallpaperRegisterMediaTimelineListener || noop;

    window.__lweApplyUserProperties = function (props) {
        var listener = window.wallpaperPropertyListener;
        if (listener && typeof listener.applyUserProperties === 'function') listener.applyUserProperties(props);
    };
    window.__lweApplyGeneralProperties = function (props) {
        var listener = window.wallpaperPropertyListener;
        if (listener && typeof listener.applyGeneralProperties === 'function') listener.applyGeneralProperties(props);
    };
})();
)JS";

class WebRenderer {
   public:
    WebRenderer(WebFrameBuffer* frame, uint32_t width, uint32_t height, uint32_t fps)
        : frame_(frame), width_(width), height_(height), fps_(fps) {}

    // The QApplication is created on the stack in main() before this runs.
    void setup(const QString& html_path, const QString& properties_json, int ctrl_fd) {
        view_.resize(static_cast<int>(width_), static_cast<int>(height_));
        QWebEngineSettings* settings = view_.settings();
        settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
        settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);

        QWebEngineScript shim;
        shim.setName(QStringLiteral("lwe-shim"));
        shim.setSourceCode(QString::fromUtf8(kShimScript));
        shim.setInjectionPoint(QWebEngineScript::DocumentCreation);
        shim.setWorldId(QWebEngineScript::MainWorld);
        shim.setRunsOnSubFrames(true);
        view_.page()->scripts().insert(shim);

        const QUrl url = QUrl::fromLocalFile(html_path);
        QObject::connect(view_.page(), &QWebEnginePage::loadFinished, [this, properties_json](bool ok) {
            if (!ok) return;
            const QString general = QStringLiteral("{\"fps\":%1}").arg(fps_);
            view_.page()->runJavaScript(QStringLiteral("window.__lweApplyGeneralProperties(%1)").arg(general));
            view_.page()->runJavaScript(QStringLiteral("window.__lweApplyUserProperties(%1)").arg(properties_json));
        });
        view_.setAttribute(Qt::WA_DontShowOnScreen, true);
        view_.show();
        view_.setUrl(url);

        render_timer_.setInterval(static_cast<int>(1000 / (fps_ > 0 ? fps_ : 60)));
        QObject::connect(&render_timer_, &QTimer::timeout, [this]() {
            pollInput();
            capture();
        });
        render_timer_.start();

        if (ctrl_fd >= 0) {
            ctrl_fd_ = ctrl_fd;
            fcntl(ctrl_fd_, F_SETFL, fcntl(ctrl_fd_, F_GETFL, 0) | O_NONBLOCK);
        }
    }

   private:
    void quit() {
        alarm(3);  // Hard deadline in case Chromium teardown stalls.
        qApp->quit();
    }

    void pollInput() {
        if (ctrl_fd_ < 0) return;
        for (;;) {
            WebInputMessage msg;
            const ssize_t n = ::recv(ctrl_fd_, &msg, sizeof(msg), MSG_DONTWAIT);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return;
                quit();
                return;
            }
            if (n == 0) {
                quit();
                return;
            }
            if (n == static_cast<ssize_t>(sizeof(msg))) handleInput(msg);
        }
    }

    void handleInput(const WebInputMessage& msg) {
        if (msg.type == WEB_INPUT_SHUTDOWN) {
            quit();
            return;
        }
        QWidget* target = view_.focusProxy() ? view_.focusProxy() : &view_;
        const QPointF pos(msg.x * static_cast<float>(width_), msg.y * static_cast<float>(height_));
        const Qt::KeyboardModifiers mods = toQtModifiers(msg.modifiers);
        switch (msg.type) {
            case WEB_INPUT_MOUSE_MOVE: {
                QMouseEvent event(QEvent::MouseMove, pos, pos, Qt::NoButton, Qt::MouseButtons(), mods);
                QCoreApplication::sendEvent(target, &event);
                break;
            }
            case WEB_INPUT_MOUSE_DOWN:
            case WEB_INPUT_MOUSE_UP: {
                const Qt::MouseButton button = toQtButton(msg.button);
                const QEvent::Type type =
                    msg.type == WEB_INPUT_MOUSE_DOWN ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease;
                const Qt::MouseButtons held = type == QEvent::MouseButtonPress ? button : Qt::MouseButtons();
                QMouseEvent event(type, pos, pos, button, held, mods);
                QCoreApplication::sendEvent(target, &event);
                break;
            }
            case WEB_INPUT_MOUSE_SCROLL: {
                const QPoint angle(static_cast<int>(msg.scroll_x * 120.0f), static_cast<int>(msg.scroll_y * 120.0f));
                QWheelEvent event(pos, pos, QPoint(), angle, Qt::NoButton, mods, Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(target, &event);
                break;
            }
            default:
                break;
        }
    }

    void capture() {
        QImage image = view_.grab().toImage();
        if (image.format() != QImage::Format_ARGB32) image = image.convertToFormat(QImage::Format_ARGB32);
        if (image.width() != static_cast<int>(width_) || image.height() != static_cast<int>(height_)) return;

        pthread_mutex_lock(&frame_->mutex);
        uint8_t* pixels = reinterpret_cast<uint8_t*>(frame_) + sizeof(WebFrameBuffer);
        const int stride = static_cast<int>(width_) * 4;
        for (uint32_t row = 0; row < height_; ++row) {
            memcpy(pixels + row * stride, image.constScanLine(static_cast<int>(row)), stride);
        }
        ++frame_->frame_counter;
        pthread_mutex_unlock(&frame_->mutex);
    }

    WebFrameBuffer* frame_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t fps_ = 60;
    QWebEngineView view_;
    QTimer render_timer_;
    int ctrl_fd_ = -1;
};

}  // namespace

int main(int argc, char** argv) {
    // offscreen QPA keeps the helper off the desktop; QtWebEngineProcess inherits
    // the same environment. Must be set before QApplication is constructed.
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    // Externally supplied values win (tests/tuning); default to GPU compositing.
    setenv("QT_QPA_PLATFORM", "offscreen", 0);
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

    QApplication app(argc, argv);
    WebRenderer renderer(frame, width, height, fps);
    renderer.setup(QString::fromUtf8(html), QString::fromUtf8(properties), ctrl_fd);
    return app.exec();
}
