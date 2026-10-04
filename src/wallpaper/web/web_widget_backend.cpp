#include "wallpaper/web/web_widget_backend.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <QApplication>
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

#include "wallpaper/web/web_input_qt.h"
#include "wallpaper/web/web_renderer_shared.h"

namespace web_renderer {

WidgetBackend::WidgetBackend(WebFrameBuffer* frame, uint32_t width, uint32_t height, uint32_t fps)
    : frame_(frame), width_(width), height_(height), fps_(fps) {}

WidgetBackend::~WidgetBackend() {
    delete render_timer_;
    delete view_;
}

bool WidgetBackend::start(const std::string& html_path, const std::string& user_properties_json, int ctrl_fd) {
    view_ = new QWebEngineView();
    view_->resize(static_cast<int>(width_), static_cast<int>(height_));
    QWebEngineSettings* settings = view_->settings();
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);

    QWebEngineScript shim;
    shim.setName(QStringLiteral("lwe-shim"));
    shim.setSourceCode(QString::fromStdString(buildShimScript(user_properties_json, static_cast<int>(fps_))));
    shim.setInjectionPoint(QWebEngineScript::DocumentCreation);
    shim.setWorldId(QWebEngineScript::MainWorld);
    shim.setRunsOnSubFrames(true);
    view_->page()->scripts().insert(shim);

    view_->setAttribute(Qt::WA_DontShowOnScreen, true);
    view_->show();
    view_->setUrl(QUrl::fromLocalFile(QString::fromStdString(html_path)));

    render_timer_ = new QTimer();
    render_timer_->setInterval(static_cast<int>(1000 / (fps_ > 0 ? fps_ : 60)));
    QObject::connect(render_timer_, &QTimer::timeout, [this]() {
        pollInput();
        capture();
    });
    render_timer_->start();

    if (ctrl_fd >= 0) {
        ctrl_fd_ = ctrl_fd;
        fcntl(ctrl_fd_, F_SETFL, fcntl(ctrl_fd_, F_GETFL, 0) | O_NONBLOCK);
    }
    return true;
}

void WidgetBackend::quit() {
    alarm(3);  // Hard deadline in case Chromium teardown stalls.
    qApp->quit();
}

void WidgetBackend::pollInput() {
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

void WidgetBackend::handleInput(const WebInputMessage& msg) {
    const RenderEvent event = decodeInput(msg);
    if (event.kind == EventKind::Shutdown) {
        quit();
        return;
    }

    QWidget* target = view_->focusProxy() ? view_->focusProxy() : view_;
    const QPointF pos(event.x * static_cast<float>(width_), event.y * static_cast<float>(height_));
    const Qt::KeyboardModifiers mods = toQtModifiers(event.modifiers);
    switch (event.kind) {
        case EventKind::MouseMove: {
            QMouseEvent qevent(QEvent::MouseMove, pos, pos, Qt::NoButton, Qt::MouseButtons(), mods);
            QCoreApplication::sendEvent(target, &qevent);
            break;
        }
        case EventKind::MouseDown:
        case EventKind::MouseUp: {
            const Qt::MouseButton button = toQtButton(event.button);
            const QEvent::Type type =
                event.kind == EventKind::MouseDown ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease;
            const Qt::MouseButtons held = type == QEvent::MouseButtonPress ? button : Qt::MouseButtons();
            QMouseEvent qevent(type, pos, pos, button, held, mods);
            QCoreApplication::sendEvent(target, &qevent);
            break;
        }
        case EventKind::Scroll: {
            const QPoint angle(static_cast<int>(event.scroll_x * 120.0f), static_cast<int>(event.scroll_y * 120.0f));
            QWheelEvent qevent(pos, pos, QPoint(), angle, Qt::NoButton, mods, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(target, &qevent);
            break;
        }
        default:
            break;
    }
}

void WidgetBackend::capture() {
    if (!view_) return;
    QImage image = view_->grab().toImage();
    if (image.format() != QImage::Format_ARGB32) image = image.convertToFormat(QImage::Format_ARGB32);
    if (image.width() != static_cast<int>(width_) || image.height() != static_cast<int>(height_)) return;
    publishFrame(frame_, image.constBits(), width_, height_);
}

}  // namespace web_renderer
