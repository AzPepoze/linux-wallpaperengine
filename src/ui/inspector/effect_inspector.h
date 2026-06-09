#ifndef EFFECT_INSPECTOR_H
#define EFFECT_INSPECTOR_H

class ShaderPass;
class Effect;
struct EngineContext;

namespace Inspector {
void showShaderPass(EngineContext& ctx, ShaderPass& pass, int id);
void showEffect(EngineContext& ctx, Effect& effect, int id);
}  // namespace Inspector

#endif  // EFFECT_INSPECTOR_H
