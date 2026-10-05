#include "wallpaper/web/web_quick_view.h"

#include <QColor>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QUrl>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWheelEvent>

#include "wallpaper/web/web_input_qt.h"
#include "wallpaper/web/web_renderer_shared.h"

namespace web_renderer {

QuickWebView createQuickWebView(QQuickWindow& window, const std::string& html_path,
                                const std::string& user_properties_json, int fps) {
    QWebEngineScript shim;
    shim.setName(QStringLiteral("lwe-shim"));
    shim.setSourceCode(QString::fromStdString(buildShimScript(user_properties_json, fps)));
    shim.setInjectionPoint(QWebEngineScript::DocumentCreation);
    shim.setWorldId(QWebEngineScript::MainWorld);
    shim.setRunsOnSubFrames(true);
    QWebEngineProfile::defaultProfile()->scripts()->insert(shim);

    window.setColor(Qt::black);

    QuickWebView view;
    view.engine = std::make_unique<QQmlEngine>();
    view.component = std::make_unique<QQmlComponent>(view.engine.get());

    const QString url = QUrl::fromLocalFile(QString::fromStdString(html_path)).toString();
    view.component->setData(QStringLiteral("import QtQuick\nimport QtWebEngine\n"
                                           "WebEngineView { width: %1; height: %2; url: \"%3\"\n"
                                           "  backgroundColor: \"#000000\"\n"
                                           "  settings.localContentCanAccessFileUrls: true\n"
                                           "  settings.localContentCanAccessRemoteUrls: true\n"
                                           "}\n")
                                .arg(window.width())
                                .arg(window.height())
                                .arg(url)
                                .toUtf8(),
                            QUrl());
    if (view.component->isError()) return {};

    QObject* root = view.component->create();
    if (!root) return {};
    view.item = qobject_cast<QQuickItem*>(root);
    if (!view.item) return {};

    view.item->setParentItem(window.contentItem());
    view.item->setWidth(window.width());
    view.item->setHeight(window.height());
    view.item->setFocus(true);
    return view;
}

void renderQuickFrame(QQuickRenderControl& control) {
    control.polishItems();
    control.beginFrame();
    control.sync();
    control.render();
    control.endFrame();
}

void sendQuickInput(QQuickWindow& window, const WebInputMessage& msg, uint32_t width, uint32_t height) {
    const RenderEvent event = decodeInput(msg);
    const QPointF pos(event.x * static_cast<float>(width), event.y * static_cast<float>(height));
    const Qt::KeyboardModifiers mods = toQtModifiers(event.modifiers);

    switch (event.kind) {
        case EventKind::MouseMove: {
            QMouseEvent qevent(QEvent::MouseMove, pos, pos, Qt::NoButton, Qt::MouseButtons(), mods);
            QCoreApplication::sendEvent(&window, &qevent);
            break;
        }
        case EventKind::MouseDown:
        case EventKind::MouseUp: {
            const Qt::MouseButton button = toQtButton(event.button);
            const QEvent::Type type =
                event.kind == EventKind::MouseDown ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease;
            const Qt::MouseButtons held = type == QEvent::MouseButtonPress ? button : Qt::MouseButtons();
            QMouseEvent qevent(type, pos, pos, button, held, mods);
            QCoreApplication::sendEvent(&window, &qevent);
            break;
        }
        case EventKind::Scroll: {
            const QPoint angle(static_cast<int>(event.scroll_x * 120.0f), static_cast<int>(event.scroll_y * 120.0f));
            QWheelEvent qevent(pos, pos, QPoint(), angle, Qt::NoButton, mods, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(&window, &qevent);
            break;
        }
        default:
            break;
    }
}

}  // namespace web_renderer
