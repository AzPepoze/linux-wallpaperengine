#ifndef WEB_INPUT_QT_H
#define WEB_INPUT_QT_H

#include <stdint.h>

#include <QtCore/Qt>

namespace web_renderer {

inline Qt::MouseButton toQtButton(uint32_t button) {
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

inline Qt::KeyboardModifiers toQtModifiers(uint32_t modifiers) {
    Qt::KeyboardModifiers out;
    if (modifiers & (1u << 0)) out |= Qt::ShiftModifier;
    if (modifiers & (1u << 1)) out |= Qt::ControlModifier;
    if (modifiers & (1u << 2)) out |= Qt::AltModifier;
    if (modifiers & (1u << 3)) out |= Qt::MetaModifier;
    return out;
}

}  // namespace web_renderer

#endif  // WEB_INPUT_QT_H
