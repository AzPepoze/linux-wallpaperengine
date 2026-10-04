#include "wallpaper/web/web_render_control.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QTimer>
#include <QUrl>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWheelEvent>
#include <memory>

#include "wallpaper/web/web_input_qt.h"
#include "wallpaper/web/web_renderer_shared.h"

namespace web_renderer {

struct RenderControlBackend::Impl {
    static constexpr int kReadbackRing = 3;

    WebFrameBuffer* frame = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 60;

    QQuickRenderControl control;
    std::unique_ptr<QQuickWindow> window;
    std::unique_ptr<QOpenGLContext> context;
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QOpenGLFramebufferObject> fbo;
    std::unique_ptr<QQmlEngine> engine;
    std::unique_ptr<QQmlComponent> component;
    QObject* root = nullptr;
    std::unique_ptr<QTimer> timer;
    int ctrl_fd = -1;

    // Asynchronous readback: each frame is copied into a pixel-buffer object and
    // picked up a couple of frames later, so the CPU never waits on the GPU.
    QOpenGLExtraFunctions* gl = nullptr;
    GLuint pbo[kReadbackRing] = {};
    GLsync fences[kReadbackRing] = {};
    size_t readback_size = 0;
    int frame_index = 0;

    Impl(WebFrameBuffer* buffer, uint32_t w, uint32_t h, uint32_t rate)
        : frame(buffer), width(w), height(h), fps(rate) {}

    ~Impl() {
        if (!gl) return;
        if (context && surface && context->makeCurrent(surface.get())) {
            for (int i = 0; i < kReadbackRing; ++i) {
                if (fences[i]) gl->glDeleteSync(fences[i]);
            }
            if (pbo[0]) gl->glDeleteBuffers(kReadbackRing, pbo);
            context->doneCurrent();
        }
    }

    void tick();
    void quit();
    void pollInput();
    void handleInput(const WebInputMessage& msg);
};

void RenderControlBackend::Impl::quit() {
    alarm(3);  // Hard deadline in case Chromium teardown stalls.
    qApp->quit();
}

void RenderControlBackend::Impl::pollInput() {
    if (ctrl_fd < 0) return;
    for (;;) {
        WebInputMessage msg;
        const ssize_t n = ::recv(ctrl_fd, &msg, sizeof(msg), MSG_DONTWAIT);
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

void RenderControlBackend::Impl::handleInput(const WebInputMessage& msg) {
    const RenderEvent event = decodeInput(msg);
    if (event.kind == EventKind::Shutdown) {
        quit();
        return;
    }

    const QPointF pos(event.x * static_cast<float>(width), event.y * static_cast<float>(height));
    const Qt::KeyboardModifiers mods = toQtModifiers(event.modifiers);
    switch (event.kind) {
        case EventKind::MouseMove: {
            QMouseEvent qevent(QEvent::MouseMove, pos, pos, Qt::NoButton, Qt::MouseButtons(), mods);
            QCoreApplication::sendEvent(window.get(), &qevent);
            break;
        }
        case EventKind::MouseDown:
        case EventKind::MouseUp: {
            const Qt::MouseButton button = toQtButton(event.button);
            const QEvent::Type type =
                event.kind == EventKind::MouseDown ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease;
            const Qt::MouseButtons held = type == QEvent::MouseButtonPress ? button : Qt::MouseButtons();
            QMouseEvent qevent(type, pos, pos, button, held, mods);
            QCoreApplication::sendEvent(window.get(), &qevent);
            break;
        }
        case EventKind::Scroll: {
            const QPoint angle(static_cast<int>(event.scroll_x * 120.0f), static_cast<int>(event.scroll_y * 120.0f));
            QWheelEvent qevent(pos, pos, QPoint(), angle, Qt::NoButton, mods, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(window.get(), &qevent);
            break;
        }
        default:
            break;
    }
}

void RenderControlBackend::Impl::tick() {
    pollInput();
    if (!window || !context || !surface || !fbo || !gl) return;

    context->makeCurrent(surface.get());
    control.polishItems();
    control.beginFrame();
    control.sync();
    control.render();
    control.endFrame();

    // Collect the frame issued two ticks ago; its GPU copy has finished by now.
    const int read_index = (frame_index + kReadbackRing - 2) % kReadbackRing;
    if (fences[read_index] &&
        gl->glClientWaitSync(fences[read_index], GL_SYNC_FLUSH_COMMANDS_BIT, 0) != GL_TIMEOUT_EXPIRED) {
        gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[read_index]);
        void* pixels = gl->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, readback_size, GL_MAP_READ_BIT);
        if (pixels) {
            publishFrame(frame, static_cast<const uint8_t*>(pixels), width, height, /*flip_y=*/true);
            gl->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        gl->glDeleteSync(fences[read_index]);
        fences[read_index] = nullptr;
    }

    // Issue this frame's copy into a free buffer (asynchronous; no CPU wait).
    const int write_index = frame_index % kReadbackRing;
    if (!fences[write_index]) {
        gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[write_index]);
        gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo->handle());
        gl->glReadBuffer(GL_COLOR_ATTACHMENT0);
        gl->glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_BGRA, GL_UNSIGNED_BYTE,
                         nullptr);
        gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        fences[write_index] = gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
    gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    ++frame_index;
}

RenderControlBackend::RenderControlBackend(WebFrameBuffer* frame, uint32_t width, uint32_t height, uint32_t fps)
    : impl_(new Impl(frame, width, height, fps)) {}

RenderControlBackend::~RenderControlBackend() {
    delete impl_;
}

bool RenderControlBackend::start(const std::string& html_path, const std::string& user_properties_json, int ctrl_fd) {
    Impl& d = *impl_;

    // Must be set before the QQuickWindow is created, or the software adaptation
    // is chosen and initialize() fails.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    d.window = std::make_unique<QQuickWindow>(&d.control);

    d.context = std::make_unique<QOpenGLContext>();
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRenderableType(QSurfaceFormat::OpenGL);
    d.context->setFormat(format);
    if (!d.context->create()) return false;

    d.surface = std::make_unique<QOffscreenSurface>();
    d.surface->setFormat(d.context->format());
    d.surface->create();
    if (!d.surface->isValid()) return false;
    if (!d.context->makeCurrent(d.surface.get())) return false;

    d.window->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(d.context.get()));
    if (!d.control.initialize()) return false;

    QOpenGLFramebufferObjectFormat fbo_format;
    fbo_format.setInternalTextureFormat(GL_RGBA8);
    d.fbo = std::make_unique<QOpenGLFramebufferObject>(static_cast<int>(d.width), static_cast<int>(d.height), fbo_format);
    if (!d.fbo->isValid()) return false;
    d.window->setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(d.fbo->texture(), d.fbo->size()));
    d.window->resize(static_cast<int>(d.width), static_cast<int>(d.height));

    d.gl = d.context->extraFunctions();
    d.readback_size = static_cast<size_t>(d.width) * d.height * 4;
    d.gl->glGenBuffers(Impl::kReadbackRing, d.pbo);
    for (int i = 0; i < Impl::kReadbackRing; ++i) {
        d.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, d.pbo[i]);
        d.gl->glBufferData(GL_PIXEL_PACK_BUFFER, d.readback_size, nullptr, GL_STREAM_READ);
    }
    d.gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    // The shim must be installed before the view loads.
    QWebEngineScript shim;
    shim.setName(QStringLiteral("lwe-shim"));
    shim.setSourceCode(QString::fromStdString(buildShimScript(user_properties_json, static_cast<int>(d.fps))));
    shim.setInjectionPoint(QWebEngineScript::DocumentCreation);
    shim.setWorldId(QWebEngineScript::MainWorld);
    shim.setRunsOnSubFrames(true);
    QWebEngineProfile::defaultProfile()->scripts()->insert(shim);

    d.engine = std::make_unique<QQmlEngine>();
    d.component = std::make_unique<QQmlComponent>(d.engine.get());
    const QString url = QUrl::fromLocalFile(QString::fromStdString(html_path)).toString();
    d.component->setData(QStringLiteral("import QtQuick\nimport QtWebEngine\n"
                                        "WebEngineView { width: %1; height: %2; url: \"%3\"\n"
                                        "  settings.localContentCanAccessFileUrls: true\n"
                                        "  settings.localContentCanAccessRemoteUrls: true\n"
                                        "}\n")
                             .arg(d.width)
                             .arg(d.height)
                             .arg(url)
                             .toUtf8(),
                         QUrl());
    if (d.component->isError()) return false;

    d.root = d.component->create();
    if (!d.root) return false;
    auto* item = qobject_cast<QQuickItem*>(d.root);
    if (!item) return false;
    item->setParentItem(d.window->contentItem());
    item->setWidth(static_cast<qreal>(d.width));
    item->setHeight(static_cast<qreal>(d.height));
    item->setFocus(true);

    d.timer = std::make_unique<QTimer>();
    d.timer->setInterval(static_cast<int>(1000 / (d.fps > 0 ? d.fps : 60)));
    QObject::connect(d.timer.get(), &QTimer::timeout, [&d]() { d.tick(); });
    d.timer->start();

    d.ctrl_fd = ctrl_fd;
    if (ctrl_fd >= 0) fcntl(ctrl_fd, F_SETFL, fcntl(ctrl_fd, F_GETFL, 0) | O_NONBLOCK);
    return true;
}

}  // namespace web_renderer
