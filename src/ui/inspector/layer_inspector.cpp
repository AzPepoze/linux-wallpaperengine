#include "layer_inspector.h"

#if DEBUG_BUILD

#include "wallpaper/2d/layers/layer.h"

namespace Inspector {

void showLayer(EngineContext& ctx, Layer& layer) {
    layer.showInspector(ctx);
}

}  // namespace Inspector

#endif  // DEBUG_BUILD
