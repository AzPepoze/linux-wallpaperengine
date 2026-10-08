#ifndef WEB_QUICK_VIEW_H
#define WEB_QUICK_VIEW_H

#include <QQmlComponent>
#include <QQmlEngine>
#include <memory>
#include <string>

#include "wallpaper/web/web_ipc.h"

class QQuickItem;
class QQuickRenderControl;
class QQuickWindow;

namespace web_renderer {

// An invisible window's QML WebEngineView plus the objects that own it.
struct QuickWebView {
    std::unique_ptr<QQmlEngine> engine;
    std::unique_ptr<QQmlComponent> component;
    QQuickItem* item = nullptr;
};

// Creates the QML WebEngineView with the Wallpaper Engine shim installed; empty on failure.
QuickWebView createQuickWebView(QQuickWindow& window, const std::string& html_path,
                                const std::string& user_properties_json, int fps);

// Runs one offscreen render pass (polish/beginFrame/sync/render/endFrame).
void renderQuickFrame(QQuickRenderControl& control);

// Forwards a control message to the window.
void sendQuickInput(QQuickWindow& window, const WebInputMessage& msg, uint32_t width, uint32_t height);

}  // namespace web_renderer

#endif  // WEB_QUICK_VIEW_H
