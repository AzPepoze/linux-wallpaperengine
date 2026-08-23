#ifndef GLOBAL_INSPECTOR_H
#define GLOBAL_INSPECTOR_H

#include "shared/core/build_config.h"

#if DEBUG_BUILD

struct EngineContext;

namespace Inspector {

class GlobalInspector {
   public:
    static void show(EngineContext& ctx);
};

}  // namespace Inspector

#endif  // DEBUG_BUILD

#endif  // GLOBAL_INSPECTOR_H
