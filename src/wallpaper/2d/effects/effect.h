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
