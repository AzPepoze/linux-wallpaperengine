#include "scene_builder.h"

#include <string>

#include "core/logger.h"
#include "formats/wallpaper_engine/scene/scene_parser.h"
#include "wallpaper/scene/2d/layers/image_layer.h"
#include "wallpaper/scene/2d/layers/particle_layer.h"

namespace {

const char* runtimeClassName(wallpaper_engine::SceneObjectKind kind) {
    switch (kind) {
        case wallpaper_engine::SceneObjectKind::Image:
            return "ImageLayer";
        case wallpaper_engine::SceneObjectKind::Particle:
            return "ParticleLayer";
        default:
            return "SceneTreeNode";
    }
}

std::string sceneTreeDisplayName(const wallpaper_engine::SceneObjectDocument& object) {
    const std::string object_name = object.name.empty() ? "Node " + std::to_string(object.node.id) : object.name;
    return "[" + std::string(runtimeClassName(object.kind)) + "] " + object_name;
}

}  // namespace

ParsedScene SceneBuilder::load(const char* scene_json_path, EngineContext& ctx) {
    wallpaper_engine::SceneDocument document;
    if (!wallpaper_engine::parseSceneFile(scene_json_path, document)) return {};
    return buildFromDocument(document, ctx);
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

    ctx.camera = document.camera;
    ctx.general = document.general;
    ctx.scene_w = out.design_width;
    ctx.scene_h = out.design_height;
    ctx.perspective_override_fov = document.general.perspective_override_fov;

    out.scene_tree = new SceneTree();
    for (const auto& object : document.objects) {
        if (!object.node.valid) continue;

        SceneTreeNode node;
        node.id = object.node.id;
        node.parent_id = object.node.parent_id;
        node.name = sceneTreeDisplayName(object);
        node.origin = object.node.origin;
        node.scale = object.node.scale;
        node.angles = object.node.angles;
        node.parallax_depth = object.node.parallax_depth;
        node.propagate_to_children = object.node.propagate_to_children;
        out.scene_tree->addNode(node);
    }
    out.scene_tree->rebuildHierarchy();

    for (const auto& object : document.objects) {
        Layer* layer = nullptr;
        if (object.kind == wallpaper_engine::SceneObjectKind::Particle) {
            layer = ParticleLayer::createFromDocument(object, ctx);
        } else if (object.kind == wallpaper_engine::SceneObjectKind::Image) {
            layer = ImageLayer::createFromDocument(object, ctx);
        }
        if (layer) out.layers.push_back(layer);
    }

    LOG_I("Built scene tree with %zu nodes and %zu layers", out.scene_tree->size(), out.layers.size());
    return out;
}

ParsedScene SceneBuilder::buildVideoScene(const char* video_path, EngineContext& ctx) {
    if (!video_path || video_path[0] == '\0') return {};

    std::string resolved_path;
    GfxImage img = ctx.asset_mgr.resolveTexture(video_path, &resolved_path);
    if (img.id == SG_INVALID_ID) {
        LOG_E("Failed to resolve video texture for wallpaper: %s", video_path);
        return {};
    }

    sg_image_desc desc = sg_query_image_desc(img);
    const float w = desc.width > 0 ? (float)desc.width : 1920.0f;
    const float h = desc.height > 0 ? (float)desc.height : 1080.0f;

    ParsedScene out;
    out.type = SCENE_TYPE_VIDEO;
    out.design_width = w;
    out.design_height = h;
    out.has_clear_color = true;
    out.clear_color[0] = 0.0f;
    out.clear_color[1] = 0.0f;
    out.clear_color[2] = 0.0f;
    out.clear_color[3] = 1.0f;

    ctx.scene_w = w;
    ctx.scene_h = h;

    auto* layer = new ImageLayer("Video Layer", std::move(img));
    layer->path = resolved_path.empty() ? video_path : resolved_path;
    layer->scene_object_id = 1;
    layer->visible = true;
    layer->size[0] = w;
    layer->size[1] = h;
    layer->origin[0] = w * 0.5f;
    layer->origin[1] = h * 0.5f;
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
    node.name = "[VideoLayer] Video Wallpaper";
    node.origin = {w * 0.5f, h * 0.5f, 0.0f};
    node.scale = {1.0f, 1.0f, 1.0f};
    node.angles = {0.0f, 0.0f, 0.0f};
    node.parallax_depth = {0.0f, 0.0f};
    node.propagate_to_children = false;
    out.scene_tree->addNode(node);
    out.scene_tree->rebuildHierarchy();

    LOG_I("Built video wallpaper scene (%ux%u): %s", (uint32_t)w, (uint32_t)h, layer->path.c_str());
    return out;
}
