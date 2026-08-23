#ifndef EFFECT_INSPECTOR_H
#define EFFECT_INSPECTOR_H

#include "shared/core/build_config.h"

#if DEBUG_BUILD

class ShaderPass;
class Effect;
struct EngineContext;

namespace Inspector {
void showShaderPass(EngineContext& ctx, ShaderPass& pass, int id);
void showEffect(EngineContext& ctx, Effect& effect, int id);
}  // namespace Inspector

#endif  // DEBUG_BUILD

#endif  // EFFECT_INSPECTOR_H
