#ifndef IMAGE_INSPECTOR_H
#define IMAGE_INSPECTOR_H

#include "shared/core/build_config.h"

#if DEBUG_BUILD

class ImageLayer;
struct EngineContext;

namespace Inspector {
void showImageLayerInspector(EngineContext& ctx, ImageLayer& layer);
}

#endif  // DEBUG_BUILD

#endif  // IMAGE_INSPECTOR_H
