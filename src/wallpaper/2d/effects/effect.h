#ifndef EFFECT_H
#define EFFECT_H

#include <cjson/cJSON.h>

#include <memory>
#include <string>
#include <vector>

#include "shared/graphics/passes/shader_pass.h"

class EngineContext;

namespace wallpaper_engine {
struct EffectInstanceDocument;
}

// While one is alive, effects start preparing their shaders on the task pool instead of compiling them one at a
// time; finish() (or destruction) completes them on the calling thread. Scene loading wraps its layer creation in one.
class EffectLoadBatch {
   public:
    explicit EffectLoadBatch(EngineContext& ctx);
    ~EffectLoadBatch();
    EffectLoadBatch(const EffectLoadBatch&) = delete;
    EffectLoadBatch& operator=(const EffectLoadBatch&) = delete;

    void finish();
    // A pass that is destroyed before the batch finishes must not be completed afterwards.
    static void forget(ShaderPass* pass);

   private:
    friend class Effect;
    EngineContext& ctx_;
    std::vector<ShaderPass*> pending_;
};

class Effect {
   public:
    std::string file_path;
    std::vector<ShaderPass*> passes;
    bool visible = true;
    bool solo = false;

    Effect(cJSON* config, EngineContext& ctx);
    ~Effect();

    Effect(const Effect&) = delete;
    Effect& operator=(const Effect&) = delete;

    static Effect* load(const char* rel_path, cJSON* instance_config, EngineContext& ctx);
    static Effect* loadFromDocument(const wallpaper_engine::EffectInstanceDocument& doc, EngineContext& ctx);
    void init(EngineContext& ctx);
};

#endif  // EFFECT_H
