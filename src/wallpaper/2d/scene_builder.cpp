#include "scene_builder.h"

#include <math.h>

#include <string>

#include "shared/core/logger.h"
#include "shared/core/phase_timer.h"
#include "shared/core/task_pool.h"
#include "wallpaper/2d/layers/image/image_layer.h"
#include "wallpaper/2d/layers/particle/particle_layer.h"
#include "wallpaper/2d/layers/sound/sound_layer.h"
#include "wallpaper/2d/layers/text/text_layer.h"
#include "wallpaper/2d/parser/scene_parser.h"
#include "wallpaper/2d/script/scene_scripts.h"

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

Layer* SceneBuilder::buildLayer(const wallpaper_engine::SceneObjectDocument& object, EngineContext& ctx) {
    switch (object.kind) {
        case wallpaper_engine::SceneObjectKind::Particle:
            return ParticleLayer::createFromDocument(object, ctx);
        case wallpaper_engine::SceneObjectKind::Image:
            return ImageLayer::createFromDocument(object, ctx);
        case wallpaper_engine::SceneObjectKind::Text:
            return TextLayer::createFromDocument(object, ctx);
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
    ParsedScene scene = buildFromDocument(document, ctx);
    batch.finish();
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
    ctx.scene.perspective_override_fov = document.general.perspective_override_fov;

    out.scene_tree = new SceneTree();
    out.scripts = new ScriptBindings(ctx);
    out.scripts->setUserProperties(ctx.user_properties);
    for (const auto& object : document.objects) {
        if (!object.node.valid) continue;

        out.scene_tree->addNode(treeNode(object));
    }
    out.scene_tree->rebuildHierarchy();

    for (const auto& object : document.objects) {
        if (Layer* layer = buildLayer(object, ctx)) out.layers.push_back(layer);
    }

    for (const auto& object : document.objects) out.scripts->addObject(object);

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
