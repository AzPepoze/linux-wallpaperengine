#ifndef PASS_UTIL_H
#define PASS_UTIL_H

#include "sokol_gfx.h"

// A pass that renders into one color attachment and keeps the result. `clear_alpha` only matters for a clear.
inline sg_pass colorPass(sg_view target, sg_load_action load, float clear_alpha = 0.0f) {
    sg_pass pass = {};
    pass.action.colors[0].load_action = load;
    pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, clear_alpha};
    pass.attachments.colors[0] = target;
    return pass;
}

#endif  // PASS_UTIL_H
