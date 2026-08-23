#ifndef LAYER_INSPECTOR_H
#define LAYER_INSPECTOR_H

#include "shared/core/build_config.h"

#if DEBUG_BUILD

class Layer;
struct EngineContext;

namespace Inspector {
void showLayer(EngineContext& ctx, Layer& layer);
}  // namespace Inspector

#endif  // DEBUG_BUILD

#endif  // LAYER_INSPECTOR_H
