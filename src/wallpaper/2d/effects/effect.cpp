#include "effect.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/passes/shader_pass.h"
#include "wallpaper/2d/parser/scene_document.h"

Effect::Effect(cJSON* config, EngineContext& ctx) : Effect(config, ctx, false) {}

Effect::Effect(cJSON* config, EngineContext& ctx, bool defer_passes) {
    cJSON* fbos_node = cJSON_GetObjectItemCaseSensitive(config, "fbos");
    if (cJSON_IsArray(fbos_node)) {
        cJSON* fbo;
        cJSON_ArrayForEach(fbo, fbos_node) {
            cJSON* name = cJSON_GetObjectItemCaseSensitive(fbo, "name");
            cJSON* scale = cJSON_GetObjectItemCaseSensitive(fbo, "scale");
            if (cJSON_IsString(name) && name->valuestring && cJSON_IsNumber(scale) && scale->valuedouble > 0.0)
                target_scales_[name->valuestring] = (float)scale->valuedouble;
        }
    }
    cJSON* functions_node = cJSON_GetObjectItemCaseSensitive(config, "functions");
    if (cJSON_IsObject(functions_node)) {
        cJSON* function;
        cJSON_ArrayForEach(function, functions_node) {
            cJSON* action = cJSON_GetObjectItemCaseSensitive(function, "action");
            cJSON* targets = cJSON_GetObjectItemCaseSensitive(function, "fbos");
            if (!function->string || !cJSON_IsString(action) || !action->valuestring ||
                strcmp(action->valuestring, "clear") != 0 || !cJSON_IsArray(targets))
                continue;
            cJSON* target;
            cJSON_ArrayForEach(target, targets) {
                if (cJSON_IsString(target) && target->valuestring)
                    functions[function->string].push_back(target->valuestring);
            }
        }
    }
    cJSON* passes_node = cJSON_GetObjectItemCaseSensitive(config, "passes");
    if (!defer_passes && cJSON_IsArray(passes_node)) {
        cJSON* pass_json;
        cJSON_ArrayForEach(pass_json, passes_node) addPassFromConfig(pass_json, nullptr, ctx);
    }
}

void Effect::addPassFromConfig(cJSON* pass_config, cJSON* instance_config, EngineContext& ctx) {
    auto* pass = new ShaderPass(pass_config, nullptr, ctx);
    cJSON* target = cJSON_GetObjectItemCaseSensitive(pass_config, "target");
    if (cJSON_IsString(target) && target->valuestring) {
        pass->render_target = target->valuestring;
        auto scale = target_scales_.find(pass->render_target);
        if (scale != target_scales_.end()) pass->render_scale = scale->second;
    }
    cJSON* bind = cJSON_GetObjectItemCaseSensitive(pass_config, "bind");
    if (cJSON_IsArray(bind)) {
        cJSON* entry;
        cJSON_ArrayForEach(entry, bind) {
            cJSON* slot = cJSON_GetObjectItemCaseSensitive(entry, "index");
            cJSON* source = cJSON_GetObjectItemCaseSensitive(entry, "name");
            if (cJSON_IsNumber(slot) && cJSON_IsString(source) && source->valuestring)
                pass->render_texture_bindings[slot->valueint] = source->valuestring;
        }
    }
    if (instance_config) {
        auto* instance_pass = new ShaderPass(pass_config, instance_config, ctx);
        instance_pass->render_target = pass->render_target;
        instance_pass->render_scale = pass->render_scale;
        instance_pass->render_texture_bindings = pass->render_texture_bindings;
        delete pass;
        pass = instance_pass;
    }
    passes.push_back(pass);
}

Effect::~Effect() {
    for (auto p : passes) {
        EffectLoadBatch::forget(p);
        delete p;
    }
    passes.clear();
}

Effect* Effect::load(const char* rel_path, cJSON* instance_config, EngineContext& ctx) {
    if (!rel_path || !rel_path[0]) return nullptr;

    char abs_path[1024];
    if (!ctx.asset_mgr->resolvePath(rel_path, abs_path, sizeof(abs_path))) {
        effect_log.warn("Effect definition not found: %s", rel_path);
        return nullptr;
    }

    char* json_str = read_file_to_string(abs_path);
    if (!json_str) return nullptr;

    cJSON* config = cJSON_Parse(json_str);
    free(json_str);
    if (!config) {
        effect_log.warn("Failed to parse effect definition: %s", rel_path);
        return nullptr;
    }

    Effect* effect = new Effect(config, ctx);
    effect->file_path = rel_path;

    cJSON* inst_passes = cJSON_GetObjectItemCaseSensitive(instance_config, "passes");
    cJSON* config_passes = cJSON_GetObjectItemCaseSensitive(config, "passes");
    if (cJSON_IsArray(inst_passes) && cJSON_IsArray(config_passes)) {
        for (int i = 0; i < cJSON_GetArraySize(inst_passes); i++) {
            if (i >= (int)effect->passes.size() || i >= cJSON_GetArraySize(config_passes)) break;
            cJSON* pass_config = cJSON_GetArrayItem(config_passes, i);
            cJSON* inst_pass_config = cJSON_GetArrayItem(inst_passes, i);
            ShaderPass* old_pass = effect->passes[i];
            auto* new_pass = new ShaderPass(pass_config, inst_pass_config, ctx);
            new_pass->render_target = old_pass->render_target;
            new_pass->render_scale = old_pass->render_scale;
            new_pass->render_texture_bindings = old_pass->render_texture_bindings;
            delete old_pass;
            effect->passes[i] = new_pass;
        }
    }

    cJSON* vis = cJSON_GetObjectItemCaseSensitive(instance_config, "visible");
    if (cJSON_IsBool(vis))
        effect->visible = cJSON_IsTrue(vis);
    else if (cJSON_IsObject(vis)) {
        cJSON* val = cJSON_GetObjectItemCaseSensitive(vis, "value");
        if (cJSON_IsBool(val)) effect->visible = cJSON_IsTrue(val);
    }

    for (size_t i = 0; i < effect->passes.size(); ++i) {
        effect->passes[i]->pass_index = (int)i;
        effect->passes[i]->effect_file = effect->file_path;
    }

    effect->init(ctx);
    cJSON_Delete(config);

    if (effect->passes.empty()) {
        effect_log.warn("Effect %s has no render passes", rel_path);
    } else {
        effect_log.info("Loaded generic Wallpaper Engine effect: %s (%zu pass%s)", rel_path, effect->passes.size(),
                        effect->passes.size() == 1 ? "" : "es");
    }
    return effect;
}

Effect* Effect::loadFromDocument(const wallpaper_engine::EffectInstanceDocument& doc, EngineContext& ctx) {
    cJSON* inst_json = nullptr;
    if (!doc.instance_config_json.empty()) inst_json = cJSON_Parse(doc.instance_config_json.c_str());

    Effect* eff = load(doc.file.c_str(), inst_json, ctx);
    if (eff) {
        eff->visible = doc.visible;
        eff->name = doc.name;
    }
    if (inst_json) cJSON_Delete(inst_json);
    return eff;
}

EffectLoadJob::EffectLoadJob(const wallpaper_engine::EffectInstanceDocument& document, EngineContext& ctx)
    : ctx_(ctx), path_(document.file), name_(document.name), visible_(document.visible) {
    if (!document.instance_config_json.empty()) instance_config_ = cJSON_Parse(document.instance_config_json.c_str());
    char abs_path[1024];
    if (path_.empty() || !ctx_.asset_mgr || !ctx_.asset_mgr->resolvePath(path_.c_str(), abs_path, sizeof(abs_path))) {
        effect_log.warn("Effect definition not found: %s", path_.c_str());
        complete_ = true;
        return;
    }
    char* json_str = read_file_to_string(abs_path);
    if (!json_str) {
        complete_ = true;
        return;
    }
    config_ = cJSON_Parse(json_str);
    free(json_str);
    if (!config_) {
        effect_log.warn("Failed to parse effect definition: %s", path_.c_str());
        complete_ = true;
        return;
    }
    effect_ = new Effect(config_, ctx_, true);
    effect_->file_path = path_;
    effect_->name = name_;
    effect_->visible = visible_;
}

EffectLoadJob::~EffectLoadJob() {
    delete effect_;
    if (instance_config_) cJSON_Delete(instance_config_);
    if (config_) cJSON_Delete(config_);
}

bool EffectLoadJob::step() {
    if (complete_) return true;
    if (!effect_) {
        complete_ = true;
        return true;
    }
    cJSON* passes = cJSON_GetObjectItemCaseSensitive(config_, "passes");
    const int pass_count = cJSON_IsArray(passes) ? cJSON_GetArraySize(passes) : 0;
    if (pass_index_ < (size_t)pass_count) {
        cJSON* pass_config = cJSON_GetArrayItem(passes, (int)pass_index_);
        cJSON* instance_passes = cJSON_GetObjectItemCaseSensitive(instance_config_, "passes");
        cJSON* instance_pass =
            cJSON_IsArray(instance_passes) ? cJSON_GetArrayItem(instance_passes, (int)pass_index_) : nullptr;
        effect_->addPassFromConfig(pass_config, instance_pass, ctx_);
        effect_->passes.back()->pass_index = (int)pass_index_;
        effect_->passes.back()->effect_file = effect_->file_path;
        effect_->initPass(pass_index_, ctx_);
        ++pass_index_;
        return false;
    }
    if (effect_->passes.empty()) {
        effect_log.warn("Effect %s has no render passes", path_.c_str());
    } else {
        effect_log.info("Loaded generic Wallpaper Engine effect: %s (%zu pass%s)", path_.c_str(),
                        effect_->passes.size(), effect_->passes.size() == 1 ? "" : "es");
    }
    complete_ = true;
    return true;
}

bool EffectLoadJob::complete() const {
    return complete_;
}

Effect* EffectLoadJob::takeResult() {
    if (!complete_) return nullptr;
    return std::exchange(effect_, nullptr);
}

std::unique_ptr<EffectLoadJob> Effect::beginLoadFromDocument(const wallpaper_engine::EffectInstanceDocument& doc,
                                                             EngineContext& ctx) {
    return std::make_unique<EffectLoadJob>(doc, ctx);
}

namespace {
EffectLoadBatch* g_active_batch = nullptr;
std::vector<EffectLoadBatch*> g_batches;
}  // namespace

EffectLoadBatch::EffectLoadBatch(EngineContext& ctx) : ctx_(ctx) {
    g_batches.push_back(this);
}

EffectLoadBatch::~EffectLoadBatch() {
    deactivate();
    g_batches.erase(std::remove(g_batches.begin(), g_batches.end(), this), g_batches.end());
}

void EffectLoadBatch::activate() {
    g_active_batch = this;
}

void EffectLoadBatch::deactivate() {
    if (g_active_batch == this) g_active_batch = nullptr;
}

void EffectLoadBatch::forget(ShaderPass* pass) {
    for (EffectLoadBatch* batch : g_batches) {
        auto& list = batch->pending_;
        list.erase(std::remove(list.begin(), list.end(), pass), list.end());
    }
}

bool EffectLoadBatch::finish() {
    return finish(std::chrono::steady_clock::time_point::max());
}

bool EffectLoadBatch::finish(std::chrono::steady_clock::time_point deadline) {
    auto it = pending_.begin();
    while (it != pending_.end() && std::chrono::steady_clock::now() < deadline) {
        ShaderPass* pass = *it;
        if (!pass || pass->preparationReady()) {
            if (pass) pass->completeInit(ctx_);
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
    return pending_.empty();
}

void Effect::init(EngineContext& ctx) {
    for (size_t i = 0; i < passes.size(); ++i) initPass(i, ctx);
}

void Effect::initPass(size_t index, EngineContext& ctx) {
    if (index >= passes.size()) return;
    ShaderPass* pass = passes[index];
    if (g_active_batch) {
        pass->initAsync(ctx);
        g_active_batch->pending_.push_back(pass);
    } else {
        pass->init(ctx);
    }
}
