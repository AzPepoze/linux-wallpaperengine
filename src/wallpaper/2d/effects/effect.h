#ifndef EFFECT_H
#define EFFECT_H

#include <cjson/cJSON.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "shared/graphics/passes/shader_pass.h"

class EngineContext;
class Effect;

namespace wallpaper_engine {
struct EffectInstanceDocument;
}

class EffectLoadJob {
   public:
    EffectLoadJob(const wallpaper_engine::EffectInstanceDocument& document, EngineContext& ctx);
    ~EffectLoadJob();
    EffectLoadJob(const EffectLoadJob&) = delete;
    EffectLoadJob& operator=(const EffectLoadJob&) = delete;
    bool step();
    bool complete() const;
    Effect* takeResult();

   private:
    EngineContext& ctx_;
    cJSON* config_ = nullptr;
    cJSON* instance_config_ = nullptr;
    Effect* effect_ = nullptr;
    size_t pass_index_ = 0;
    bool complete_ = false;
    std::string path_;
    std::string name_;
    bool visible_ = true;
};

class EffectLoadBatch {
   public:
    explicit EffectLoadBatch(EngineContext& ctx);
    ~EffectLoadBatch();
    EffectLoadBatch(const EffectLoadBatch&) = delete;
    EffectLoadBatch& operator=(const EffectLoadBatch&) = delete;

    // Completes only shader passes whose CPU preparation has finished. Safe to call each frame.
    bool finish();
    bool finish(std::chrono::steady_clock::time_point deadline);
    size_t pendingCount() const {
        return pending_.size();
    }
    void activate();
    void deactivate();
    static void forget(ShaderPass* pass);

   private:
    friend class Effect;
    EngineContext& ctx_;
    std::vector<ShaderPass*> pending_;
};

class Effect {
    friend class EffectLoadJob;

   public:
    std::string file_path;
    std::string name;
    std::vector<ShaderPass*> passes;
    // Functions the effect defines for scripts (executeMaterialFunction): name to the render targets it clears.
    std::map<std::string, std::vector<std::string>> functions;
    bool visible = true;
    bool solo = false;

    Effect(cJSON* config, EngineContext& ctx);
    ~Effect();

    Effect(const Effect&) = delete;
    Effect& operator=(const Effect&) = delete;

    static Effect* load(const char* rel_path, cJSON* instance_config, EngineContext& ctx);
    static Effect* loadFromDocument(const wallpaper_engine::EffectInstanceDocument& doc, EngineContext& ctx);
    static std::unique_ptr<EffectLoadJob> beginLoadFromDocument(const wallpaper_engine::EffectInstanceDocument& doc,
                                                                EngineContext& ctx);
    void init(EngineContext& ctx);

   private:
    Effect(cJSON* config, EngineContext& ctx, bool defer_passes);
    void addPassFromConfig(cJSON* pass_config, cJSON* instance_config, EngineContext& ctx);
    void initPass(size_t index, EngineContext& ctx);
    std::map<std::string, float> target_scales_;
};

#endif  // EFFECT_H
