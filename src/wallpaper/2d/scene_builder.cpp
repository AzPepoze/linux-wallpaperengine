#include "scene_builder.h"

#include <math.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

#include "shared/assets/asset_manager.h"
#include "shared/core/load_trace.h"
#include "shared/core/logger.h"
#include "shared/core/phase_timer.h"
#include "shared/core/task_pool.h"
#include "wallpaper/2d/effects/effect.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/layers/particle/particle_layer.h"
#include "wallpaper/2d/layers/sound/sound_layer.h"
#include "wallpaper/2d/layers/text/text_layer.h"
#include "wallpaper/2d/parser/scene_parser.h"
#include "wallpaper/2d/script/scene_scripts.h"
#include "wallpaper/2d/script/script_engine.h"

namespace {

const char* runtimeClassName(wallpaper_engine::SceneObjectKind kind) {
    switch (kind) {
        case wallpaper_engine::SceneObjectKind::Image:
            return "ImageLayer";
        case wallpaper_engine::SceneObjectKind::Particle:
            return "ParticleLayer";
        case wallpaper_engine::SceneObjectKind::Text:
            return "TextLayer";
        case wallpaper_engine::SceneObjectKind::Sound:
            return "SoundLayer";
        default:
            return "SceneTreeNode";
    }
}

std::string sceneTreeDisplayName(const wallpaper_engine::SceneObjectDocument& object) {
    const std::string object_name = object.name.empty() ? "Node " + std::to_string(object.node.id) : object.name;
    return "[" + std::string(runtimeClassName(object.kind)) + "] " + object_name;
}

}  // namespace

SceneBuildJob::SceneBuildJob(wallpaper_engine::SceneDocument document, EngineContext& ctx)
    : document_(std::move(document)), ctx_(ctx), text_cache_(std::make_shared<TextTextureCache>()) {
    result_.camera = document_.camera;
    result_.general = document_.general;
    result_.design_width = document_.design_width;
    result_.design_height = document_.design_height;
    result_.has_clear_color = document_.general.has_clear_color;
    for (int i = 0; i < 4; ++i) result_.clear_color[i] = document_.general.clear_color[i];
    result_.camera_parallax_enabled = document_.general.camera_parallax_enabled;
    result_.camera_parallax_amount = document_.general.camera_parallax_amount;
    result_.camera_parallax_delay = document_.general.camera_parallax_delay;
    result_.camera_parallax_mouse_influence = document_.general.camera_parallax_mouse_influence;
    result_.camera_shake_enabled = document_.general.camera_shake_enabled;
    result_.camera_shake_amplitude = document_.general.camera_shake_amplitude;
    result_.camera_shake_speed = document_.general.camera_shake_speed;
    result_.camera_shake_roughness = document_.general.camera_shake_roughness;
    ctx_.scene.camera = document_.camera;
    ctx_.scene.general = document_.general;
    ctx_.scene.scene_w = document_.design_width;
    ctx_.scene.scene_h = document_.design_height;
    ctx_.scene.bound_user_keys = document_.user_keys;
    if (!document_.user_keys.empty()) ctx_.scene.bound_objects = document_.objects;
    ctx_.scene.perspective_override_fov = document_.general.perspective_override_fov;
    result_.scene_tree = new SceneTree();
    result_.scripts = new ScriptBindings(ctx_);
    result_.scripts->setUserProperties(ctx_.user_properties);
    effect_batch_ = new EffectLoadBatch(ctx_);
}

SceneBuildJob::~SceneBuildJob() {
    if (!result_taken_) {
        if (effect_batch_) effect_batch_->activate();
        releaseResult();
        if (effect_batch_) effect_batch_->deactivate();
    }
    delete effect_batch_;
}

void SceneBuildJob::releaseResult() {
    delete result_.scripts;
    result_.scripts = nullptr;
    for (Layer* layer : result_.layers) delete layer;
    result_.layers.clear();
    delete result_.scene_tree;
    result_.scene_tree = nullptr;
}

bool SceneBuildJob::step(std::chrono::milliseconds budget) {
    if (phase_ == Phase::Complete || phase_ == Phase::Cancelled) return true;
    ScriptEngine::instance().setActiveScope(nullptr);
    ScriptEngine::instance().setCreationScope(result_.scripts);
    const auto deadline = std::chrono::steady_clock::now() + std::max(budget, std::chrono::milliseconds(1));
    bool did_work = false;
    while (phase_ != Phase::Complete && phase_ != Phase::Cancelled &&
           (!did_work || std::chrono::steady_clock::now() < deadline)) {
        did_work = true;
        const auto operation_start = std::chrono::steady_clock::now();
        const auto trace_phase = phase_;
        const auto trace_layer_index = layer_index_;
        switch (phase_) {
            case Phase::Tree:
                while (tree_index_ < document_.objects.size() && !document_.objects[tree_index_].node.valid)
                    ++tree_index_;
                if (tree_index_ == document_.objects.size()) {
                    phase_ = Phase::Hierarchy;
                    break;
                }
                result_.scene_tree->addNode(SceneBuilder::treeNode(document_.objects[tree_index_++]));
                break;
            case Phase::Hierarchy:
                result_.scene_tree->rebuildHierarchy();
                phase_ = Phase::Layers;
                break;
            case Phase::Layers: {
                if (layer_index_ == document_.objects.size()) {
                    phase_ = Phase::TextTextures;
                    break;
                }
                const auto& object = document_.objects[layer_index_];
                if (object.kind == wallpaper_engine::SceneObjectKind::Image) {
                    if (ctx_.asset_mgr && !object.image.image.empty() && object.image.image[0] != '$' &&
                        (!ctx_.asset_mgr->textureReady(object.image.image.c_str(), 0) ||
                         (object.image.image.find(".tex") != std::string::npos &&
                          !ctx_.asset_mgr->textureReady(object.image.image.c_str(), 1)))) {
                        phase_ = Phase::Texture;
                        break;
                    }
                    effect_batch_->activate();
                    ImageLayer* layer = ImageLayer::createBaseFromDocument(object, ctx_);
                    effect_batch_->deactivate();
                    ++layer_index_;
                    if (layer) {
                        result_.layers.push_back(layer);
                        if (!object.effects.empty()) {
                            staged_image_layer_ = layer;
                            image_effect_index_ = 0;
                            phase_ = Phase::ImageEffects;
                        }
                    }
                } else {
                    effect_batch_->activate();
                    Layer* layer = object.kind == wallpaper_engine::SceneObjectKind::Text
                                       ? TextLayer::createPending(object, ctx_, text_cache_)
                                       : SceneBuilder::buildLayer(object, ctx_);
                    effect_batch_->deactivate();
                    ++layer_index_;
                    if (layer) result_.layers.push_back(layer);
                }
                break;
            }
            case Phase::TextTextures: {
                // One layer per operation, so each render-thread upload is followed by a budget check.
                if (text_index_ == result_.layers.size()) {
                    phase_ = Phase::Scripts;
                    break;
                }
                TextLayer* text = dynamic_cast<TextLayer*>(result_.layers[text_index_]);
                if (text && !text->pollPreparation()) return false;
                ++text_index_;
                break;
            }
            case Phase::Texture: {
                const auto texture_start = std::chrono::steady_clock::now();
                const std::string& image_path = document_.objects[layer_index_].image.image;
                const bool ready = ctx_.asset_mgr && ctx_.asset_mgr->textureReady(image_path.c_str(), 0) &&
                                   (image_path.find(".tex") == std::string::npos ||
                                    ctx_.asset_mgr->textureReady(image_path.c_str(), 1));
                const auto texture_elapsed = std::chrono::steady_clock::now() - texture_start;
                if (texture_elapsed > std::chrono::milliseconds(2))
                    LOG_TAG_W("SCENE_2D", "Texture readiness check exceeded 2ms budget (%.2fms)",
                              std::chrono::duration<double, std::milli>(texture_elapsed).count());
                if (!ready) return false;
                phase_ = Phase::Layers;
                break;
            }
            case Phase::ImageEffects: {
                if (image_effect_index_ >= document_.objects[layer_index_ - 1].effects.size()) {
                    staged_image_layer_ = nullptr;
                    phase_ = Phase::Layers;
                    break;
                }
                if (!staged_effect_job_) {
                    staged_effect_job_ = Effect::beginLoadFromDocument(
                        document_.objects[layer_index_ - 1].effects[image_effect_index_], ctx_);
                    break;
                }
                effect_batch_->activate();
                const bool effect_done = staged_effect_job_->step();
                effect_batch_->deactivate();
                if (effect_done) {
                    staged_image_layer_->addEffect(staged_effect_job_->takeResult());
                    staged_effect_job_.reset();
                    ++image_effect_index_;
                }
                break;
            }
            case Phase::Scripts:
                if (script_index_ == document_.objects.size()) {
                    phase_ = Phase::ZoomScript;
                    break;
                }
                result_.scripts->addObject(document_.objects[script_index_++]);
                break;
            case Phase::ZoomScript:
                if (!document_.general.zoom_script.empty())
                    result_.scripts->add(0, BoundProperty::SceneZoom, document_.general.zoom_script.script,
                                         document_.general.zoom_script.properties_json);
                phase_ = Phase::Effects;
                break;
            case Phase::Effects:
                if (effect_batch_->finish(deadline)) {
                    phase_ = Phase::Complete;
                    LOG_I("Built scene tree with %zu nodes, %zu layers and %zu property scripts",
                          result_.scene_tree->size(), result_.layers.size(), result_.scripts->size());
                } else {
                    // Readiness polling is cheap and can be retried on the next frame.
                }
                break;
            case Phase::Complete:
                break;
            case Phase::CleanupEffects:
                staged_effect_job_.reset();
                // CPU jobs own their inputs; discard stale passes without finalizing them.
                phase_ = Phase::CleanupScripts;
                break;
            case Phase::CleanupScripts:
                delete result_.scripts;
                result_.scripts = nullptr;
                phase_ = Phase::CleanupLayers;
                break;
            case Phase::CleanupLayers:
                if (result_.layers.empty()) {
                    phase_ = Phase::CleanupTree;
                    break;
                }
                effect_batch_->activate();
                delete result_.layers.back();
                effect_batch_->deactivate();
                result_.layers.pop_back();
                break;
            case Phase::CleanupTree:
                delete result_.scene_tree;
                result_.scene_tree = nullptr;
                staged_image_layer_ = nullptr;
                phase_ = Phase::Cancelled;
                break;
            case Phase::Cancelled:
                break;
        }
        const auto operation_elapsed = std::chrono::steady_clock::now() - operation_start;
        if (load_trace::enabled() && trace_phase == Phase::Layers && trace_layer_index < document_.objects.size())
            LOG_TAG_I("LOAD_TRACE", "layer_kind=%d layer_index=%zu operation_ms=%.3f",
                      (int)document_.objects[trace_layer_index].kind, trace_layer_index,
                      load_trace::milliseconds(operation_elapsed));
        if (load_trace::enabled())
            LOG_TAG_I("LOAD_TRACE", "scene_phase=%d operation_ms=%.3f", (int)trace_phase,
                      load_trace::milliseconds(operation_elapsed));
        if (operation_elapsed > std::chrono::milliseconds(2)) {
            LOG_TAG_W("SCENE_2D", "Scene build operation exceeded 2ms budget (%.2fms)",
                      std::chrono::duration<double, std::milli>(operation_elapsed).count());
        }
    }
    return phase_ == Phase::Complete || phase_ == Phase::Cancelled;
}

bool SceneBuildJob::complete() const {
    return phase_ == Phase::Complete;
}

bool SceneBuildJob::cancelled() const {
    return phase_ == Phase::Cancelled;
}

void SceneBuildJob::cancel() {
    if (phase_ == Phase::Complete || phase_ == Phase::Cancelled) return;
    phase_ = Phase::CleanupEffects;
}

ParsedScene SceneBuildJob::takeResult() {
    if (!complete() || result_taken_) return {};
    result_taken_ = true;
    return std::move(result_);
}

std::unique_ptr<SceneBuildJob> SceneBuilder::beginIncremental(wallpaper_engine::SceneDocument document,
                                                              EngineContext& ctx) {
    return std::make_unique<SceneBuildJob>(std::move(document), ctx);
}

SceneTreeNode SceneBuilder::treeNode(const wallpaper_engine::SceneObjectDocument& object) {
    SceneTreeNode node;
    node.id = object.node.id;
    node.parent_id = object.node.parent_id;
    node.name = sceneTreeDisplayName(object);
    node.attachment = object.node.attachment;
    node.origin = object.node.origin;
    node.scale = object.node.scale;
    node.angles = object.node.angles;
    for (float& angle : node.angles) angle *= (float)(180.0 / M_PI);
    node.parallax_depth = object.node.parallax_depth;
    node.propagate_to_children = object.node.propagate_to_children;
    if (object.kind == wallpaper_engine::SceneObjectKind::Unknown) node.visible = object.visible;
    return node;
}

Layer* SceneBuilder::buildLayer(const wallpaper_engine::SceneObjectDocument& object, EngineContext& ctx,
                                std::shared_ptr<TextTextureCache> text_cache) {
    switch (object.kind) {
        case wallpaper_engine::SceneObjectKind::Particle:
            return ParticleLayer::createFromDocument(object, ctx);
        case wallpaper_engine::SceneObjectKind::Image:
            return ImageLayer::createFromDocument(object, ctx);
        case wallpaper_engine::SceneObjectKind::Text:
            return TextLayer::createFromDocument(object, ctx, std::move(text_cache));
        case wallpaper_engine::SceneObjectKind::Sound:
            return SoundLayer::createFromDocument(object, ctx);
        default:
            return nullptr;
    }
}

ParsedScene SceneBuilder::load(const char* scene_json_path, EngineContext& ctx) {
    wallpaper_engine::SceneDocument document;
    {
        PhaseTimer timer("scene.json parse");
        if (!wallpaper_engine::parseSceneFile(scene_json_path, document, &ctx.user_properties)) return {};
    }
    PhaseTimer timer("scene build (layers, textures, effects)");
    EffectLoadBatch batch(ctx);
    batch.activate();
    ParsedScene scene = buildFromDocument(document, ctx);
    batch.deactivate();
    while (!batch.finish()) std::this_thread::yield();
    return scene;
}

ParsedScene SceneBuilder::buildFromDocument(const wallpaper_engine::SceneDocument& document, EngineContext& ctx) {
    ParsedScene out;

    out.camera = document.camera;
    out.general = document.general;
    out.design_width = document.design_width;
    out.design_height = document.design_height;
    out.has_clear_color = document.general.has_clear_color;
    for (int i = 0; i < 4; ++i) out.clear_color[i] = document.general.clear_color[i];
    out.camera_parallax_enabled = document.general.camera_parallax_enabled;
    out.camera_parallax_amount = document.general.camera_parallax_amount;
    out.camera_parallax_delay = document.general.camera_parallax_delay;
    out.camera_parallax_mouse_influence = document.general.camera_parallax_mouse_influence;
    out.camera_shake_enabled = document.general.camera_shake_enabled;
    out.camera_shake_amplitude = document.general.camera_shake_amplitude;
    out.camera_shake_speed = document.general.camera_shake_speed;
    out.camera_shake_roughness = document.general.camera_shake_roughness;

    ctx.scene.camera = document.camera;
    ctx.scene.general = document.general;
    ctx.scene.scene_w = out.design_width;
    ctx.scene.scene_h = out.design_height;
    ctx.scene.bound_user_keys = document.user_keys;
    if (!document.user_keys.empty()) ctx.scene.bound_objects = document.objects;
    ctx.scene.perspective_override_fov = document.general.perspective_override_fov;

    out.scene_tree = new SceneTree();
    out.scripts = new ScriptBindings(ctx);
    out.scripts->setUserProperties(ctx.user_properties);
    for (const auto& object : document.objects) {
        if (!object.node.valid) continue;

        out.scene_tree->addNode(treeNode(object));
    }
    out.scene_tree->rebuildHierarchy();

    const auto text_cache = std::make_shared<TextTextureCache>();
    for (const auto& object : document.objects) {
        if (Layer* layer = buildLayer(object, ctx, text_cache)) out.layers.push_back(layer);
    }

    for (const auto& object : document.objects) out.scripts->addObject(object);
    if (!document.general.zoom_script.empty())
        out.scripts->add(0, BoundProperty::SceneZoom, document.general.zoom_script.script,
                         document.general.zoom_script.properties_json);

    LOG_I("Built scene tree with %zu nodes, %zu layers and %zu property scripts", out.scene_tree->size(),
          out.layers.size(), out.scripts->size());
    return out;
}

ParsedScene SceneBuilder::buildVideoScene(const char* video_path, EngineContext& ctx) {
    if (!video_path || video_path[0] == '\0') return {};

    std::string resolved_path;
    GfxImage img = ctx.asset_mgr->resolveTexture(video_path, &resolved_path);
    if (img.id == SG_INVALID_ID) {
        LOG_E("Failed to resolve video texture for wallpaper: %s", video_path);
        return {};
    }

    sg_image_desc desc = sg_query_image_desc(img);
    const float w = desc.width > 0 ? (float)desc.width : 1920.0f;
    const float h = desc.height > 0 ? (float)desc.height : 1080.0f;

    ParsedScene out = buildImageScene("[VideoLayer] Video Wallpaper", std::move(img), w, h, SCENE_TYPE_VIDEO,
                                      resolved_path.empty() ? video_path : resolved_path.c_str(), ctx);

    auto* layer = out.layers.empty() ? nullptr : static_cast<ImageLayer*>(out.layers.front());
    if (layer) {
        const auto* v = ctx.asset_mgr->findVideoTexture(layer->img);
        if (!v && !layer->path.empty()) v = ctx.asset_mgr->findVideoTexture(layer->path);
        if (v && v->decoder) layer->bound_video_decoder = v->decoder.get();

        LOG_I("Built video wallpaper scene (%ux%u): %s", (uint32_t)w, (uint32_t)h, layer->path.c_str());
    }
    return out;
}

ParsedScene SceneBuilder::buildImageScene(const char* label, GfxImage image, float width, float height,
                                          scene_type_t type, const char* path, EngineContext& ctx) {
    ParsedScene out;
    out.type = type;
    out.design_width = width;
    out.design_height = height;
    out.has_clear_color = true;
    out.clear_color[0] = 0.0f;
    out.clear_color[1] = 0.0f;
    out.clear_color[2] = 0.0f;
    out.clear_color[3] = 1.0f;

    ctx.scene.scene_w = width;
    ctx.scene.scene_h = height;

    auto* layer = new ImageLayer(label, std::move(image));
    layer->path = path ? path : "";
    layer->scene_object_id = 1;
    layer->visible = true;
    layer->size[0] = width;
    layer->size[1] = height;
    layer->origin[0] = width * 0.5f;
    layer->origin[1] = height * 0.5f;
    layer->origin[2] = 0.0f;
    layer->scale[0] = 1.0f;
    layer->scale[1] = 1.0f;
    layer->scale[2] = 1.0f;
    layer->tint[0] = 1.0f;
    layer->tint[1] = 1.0f;
    layer->tint[2] = 1.0f;
    layer->tint[3] = 1.0f;

    sg_view_desc view_desc = {};
    view_desc.texture.image = layer->img;
    layer->cached_view = sg_make_view(&view_desc);

    out.layers.push_back(layer);

    out.scene_tree = new SceneTree();
    SceneTreeNode node;
    node.id = 1;
    node.parent_id = 0;
    node.name = label;
    node.origin = {width * 0.5f, height * 0.5f, 0.0f};
    node.scale = {1.0f, 1.0f, 1.0f};
    node.angles = {0.0f, 0.0f, 0.0f};
    node.parallax_depth = {0.0f, 0.0f};
    node.propagate_to_children = false;
    out.scene_tree->addNode(node);
    out.scene_tree->rebuildHierarchy();

    return out;
}
