#ifndef SCENE_BUILDER_H
#define SCENE_BUILDER_H

#include <chrono>
#include <memory>
#include <vector>

#include "shared/core/engine_context.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/parser/scene_document.h"
#include "wallpaper/2d/tree/scene_tree.h"

class ImageLayer;
class TextTextureCache;

struct ParsedScene {
    std::vector<Layer*> layers;
    SceneTree* scene_tree = nullptr;
    ScriptBindings* scripts = nullptr;
    wallpaper_engine::SceneCameraDocument camera;
    wallpaper_engine::SceneGeneralDocument general;
    float design_width = 0.0f;
    float design_height = 0.0f;
    float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool has_clear_color = false;
    bool camera_parallax_enabled = false;
    float camera_parallax_amount = 0.0f;
    float camera_parallax_delay = 0.1f;
    float camera_parallax_mouse_influence = 0.0f;
    bool camera_shake_enabled = false;
    float camera_shake_amplitude = 0.0f;
    float camera_shake_speed = 0.0f;
    float camera_shake_roughness = 0.0f;
    scene_type_t type = SCENE_TYPE_2D;
};

// Render-thread construction task for an already parsed document. Each step
// stops between objects once its time budget has elapsed.
class SceneBuildJob {
   public:
    SceneBuildJob(wallpaper_engine::SceneDocument document, EngineContext& ctx);
    ~SceneBuildJob();
    SceneBuildJob(const SceneBuildJob&) = delete;
    SceneBuildJob& operator=(const SceneBuildJob&) = delete;

    bool step(std::chrono::milliseconds budget);
    void cancel();
    bool complete() const;
    bool cancelled() const;
    ParsedScene takeResult();

   private:
    wallpaper_engine::SceneDocument document_;
    EngineContext& ctx_;
    ParsedScene result_;
    // Text textures are shared by the layers of this one scene load.
    std::shared_ptr<TextTextureCache> text_cache_;
    size_t text_index_ = 0;  // next layer to check in the TextTextures phase
    class EffectLoadBatch* effect_batch_ = nullptr;
    size_t tree_index_ = 0;
    size_t layer_index_ = 0;
    size_t script_index_ = 0;
    enum class Phase {
        Tree,
        Hierarchy,
        Layers,
        Texture,
        ImageEffects,
        TextTextures,
        Scripts,
        ZoomScript,
        Effects,
        Complete,
        CleanupEffects,
        CleanupScripts,
        CleanupLayers,
        CleanupTree,
        Cancelled
    } phase_ = Phase::Tree;
    ImageLayer* staged_image_layer_ = nullptr;
    size_t image_effect_index_ = 0;
    std::unique_ptr<EffectLoadJob> staged_effect_job_;
    bool result_taken_ = false;
    void releaseResult();
};

class SceneBuilder {
   public:
    static std::unique_ptr<SceneBuildJob> beginIncremental(wallpaper_engine::SceneDocument document,
                                                           EngineContext& ctx);
    static ParsedScene buildFromDocument(const wallpaper_engine::SceneDocument& document, EngineContext& ctx);
    static ParsedScene buildImageScene(const char* label, GfxImage image, float width, float height, scene_type_t type,
                                       const char* path, EngineContext& ctx);
    static ParsedScene buildVideoScene(const char* video_path, EngineContext& ctx);
    static ParsedScene load(const char* scene_json_path, EngineContext& ctx);

    static SceneTreeNode treeNode(const wallpaper_engine::SceneObjectDocument& object);
    // Null for objects that have no layer (groups).
    static Layer* buildLayer(const wallpaper_engine::SceneObjectDocument& object, EngineContext& ctx,
                             std::shared_ptr<TextTextureCache> text_cache = {});
};

#endif  // SCENE_BUILDER_H
