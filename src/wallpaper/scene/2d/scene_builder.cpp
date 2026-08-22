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

    out.design_width = document.design_width;
    out.design_height = document.design_height;
    out.has_clear_color = document.has_clear_color;
    for (int i = 0; i < 4; ++i) out.clear_color[i] = document.clear_color[i];
    out.camera_parallax_enabled = document.camera_parallax_enabled;
    out.camera_parallax_amount = document.camera_parallax_amount;
    out.camera_parallax_delay = document.camera_parallax_delay;
    out.camera_parallax_mouse_influence = document.camera_parallax_mouse_influence;
    out.camera_shake_enabled = document.camera_shake_enabled;
    out.camera_shake_amplitude = document.camera_shake_amplitude;
    out.camera_shake_speed = document.camera_shake_speed;
    out.camera_shake_roughness = document.camera_shake_roughness;

    ctx.scene_w = out.design_width;
    ctx.scene_h = out.design_height;
    ctx.perspective_override_fov = document.perspective_override_fov;

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
