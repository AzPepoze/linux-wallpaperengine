#ifndef HIERARCHY_PANEL_H
#define HIERARCHY_PANEL_H

#include "shared/core/build_config.h"

#if DEBUG_BUILD

struct EngineContext;

namespace HierarchyPanel {
void draw(EngineContext& ctx);
}

#endif  // DEBUG_BUILD

#endif  // HIERARCHY_PANEL_H
