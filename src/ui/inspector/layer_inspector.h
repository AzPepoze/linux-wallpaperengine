#ifndef LAYER_INSPECTOR_H
#define LAYER_INSPECTOR_H

class Layer;

struct EngineContext;

namespace Inspector {
void showLayer(EngineContext& ctx, Layer& layer);
}

#endif // LAYER_INSPECTOR_H
