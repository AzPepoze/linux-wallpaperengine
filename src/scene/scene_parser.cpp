#include "scene_parser.h"

#include "../core/logger.h"
#include "../formats/wallpaper_engine/scene/scene_parser.h"
#include "2d/image_layer.h"
#include "2d/particle_layer.h"

ParsedScene SceneParser::parse(const char* scene_json_path, EngineContext& ctx) {
    ParsedScene out;
    wallpaper_engine::SceneDocument document;
    if (!wallpaper_engine::parseSceneFile(scene_json_path, document)) return out;

    out.design_width = document.design_width;
    out.design_height = document.design_height;
    out.has_clear_color = document.has_clear_color;
    for (int i = 0; i < 4; ++i) out.clear_color[i] = document.clear_color[i];
    out.camera_parallax_enabled = document.camera_parallax_enabled;
    out.camera_parallax_amount = document.camera_parallax_amount;
    out.camera_parallax_delay = document.camera_parallax_delay;
    out.camera_parallax_mouse_influence = document.camera_parallax_mouse_influence;

    // Layer constructors (notably particles) need the authored orthographic
    // extent while runtime objects are instantiated.
    ctx.scene_w = out.design_width;
    ctx.scene_h = out.design_height;

    out.scene_graph = new SceneGraph();
    for (const auto& object : document.objects) {
        if (!object.node.valid) continue;

        SceneGraphNode node;
        node.id = object.node.id;
        node.parent_id = object.node.parent_id;
        node.origin = object.node.origin;
        node.scale = object.node.scale;
        node.angles = object.node.angles;
        node.parallax_depth = object.node.parallax_depth;
        node.propagate_to_children = object.node.propagate_to_children;
        out.scene_graph->addNode(node);
    }
    out.scene_graph->rebuildHierarchy();

    // Transitional adapter: legacy layer factories still consume cJSON. The
    // Wallpaper Engine parser has already finished; the source payload keeps
    // behavior stable until typed layer constructors replace this path.
    for (const auto& object : document.objects) {
        if (object.source_json.empty()) continue;
        cJSON* object_json = cJSON_Parse(object.source_json.c_str());
        if (!object_json) continue;

        Layer* layer = createLayer(object_json, ctx);
        if (layer) out.layers.push_back(layer);
        cJSON_Delete(object_json);
    }

    LOG_I("Built scene graph with %zu nodes", out.scene_graph->size());
    return out;
}

Layer* SceneParser::createLayer(cJSON* obj_json, EngineContext& ctx) {
    Layer* layer = ParticleLayer::createFromJSON(obj_json, ctx);
    if (!layer) layer = ImageLayer::createFromJSON(obj_json, ctx);
    return layer;
}
