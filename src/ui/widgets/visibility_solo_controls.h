#ifndef VISIBILITY_SOLO_CONTROLS_H
#define VISIBILITY_SOLO_CONTROLS_H

class Layer;

namespace UiWidgets {

void drawVisibilitySoloControls(bool& visible, bool& solo, const char* visibility_tooltip, const char* solo_tooltip);
void drawVisibilitySoloControls(Layer& layer, const char* visibility_tooltip, const char* solo_tooltip);

}  // namespace UiWidgets

#endif  // VISIBILITY_SOLO_CONTROLS_H
