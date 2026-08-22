#ifndef LAYER_INSPECTOR_H
#define LAYER_INSPECTOR_H

class Layer;

struct EngineContext;

namespace Inspector {
void showGlobalSettings(EngineContext& ctx);
void showLayer(EngineContext& ctx, Layer& layer);
}  // namespace Inspector

#endif  // LAYER_INSPECTOR_H
