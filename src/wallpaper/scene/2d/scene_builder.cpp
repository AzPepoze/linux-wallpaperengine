#include "scene_builder.h"

#include "core/logger.h"
#include "formats/wallpaper_engine/scene/scene_parser.h"
#include "wallpaper/scene/2d/layers/image_layer.h"
#include "wallpaper/scene/2d/layers/particle_layer.h"

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

    ctx.scene_w = out.design_width;
    ctx.scene_h = out.design_height;

    out.scene_tree = new SceneTree();
    for (const auto& object : document.objects) {
        if (!object.node.valid) continue;

        SceneTreeNode node;
        node.id = object.node.id;
        node.parent_id = object.node.parent_id;
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
