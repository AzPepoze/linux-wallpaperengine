#include "particle_layer.h"

#include "shared/core/context.h"
#include "shared/core/engine_context.h"
#include "shared/core/utils.h"
#include "wallpaper/2d/camera/parallax.h"
#include "wallpaper/2d/parser/particle_parser.h"
#include "wallpaper/2d/tree/scene_tree.h"

ParticleLayer::ParticleLayer(const char* name, ParticleSystem* ps) : Layer(name), ps(ps) {}

ParticleLayer::~ParticleLayer() {
    if (ps) delete ps;
}

ParticleLayer* ParticleLayer::createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx) {
    const ParticleObjectConfig config = ParticleParser::parseObject(doc);
    if (config.particle_path.empty()) return nullptr;
    ParticleSystem* ps = ParticleSystem::createFromPath(
        config.particle_path.c_str(), ctx, ctx.scene_w, ctx.scene_h, config.override_alpha, config.override_rate,
        config.has_override_color ? config.override_color : nullptr, config.override_color_is_legacy);
    if (ps) {
        ParticleLayer* layer = new ParticleLayer(config.name.c_str(), ps);
        layer->initFromDocument(doc, ctx);
        layer->path = ps->config_path;
        return layer;
    }
    return nullptr;
}

void ParticleLayer::update(float dt, EngineContext& ctx) {
    (void)ctx;
    if (ps) ps->update(dt);
}

void ParticleLayer::draw(EngineContext& ctx) {
    if (!ps) return;

    float layer_origin[3] = {origin[0], origin[1], origin[2]};
    float layer_scale[3] = {scale[0], scale[1], scale[2]};
    float layer_rotation = rotation;
    if (scene_object_id != 0 && ctx.scene_tree) {
        if (const SceneTreeNode* node = ctx.scene_tree->find(scene_object_id)) {
            layer_scale[0] = node->scale[0];
            layer_scale[1] = node->scale[1];
            layer_scale[2] = node->scale[2];
            layer_rotation = node->angles[2];
        }
        ctx.scene_tree->worldPosition(scene_object_id, layer_origin);
    }

    const parallax_offset_t camera_offset = parallax_layer_offset(ctx, scene_object_id, layer_origin, parallax);

    ps->layer_origin[0] = layer_origin[0] + camera_offset.x;
    ps->layer_origin[1] = layer_origin[1] + camera_offset.y;
    ps->layer_origin[2] = layer_origin[2];
    ps->layer_scale[0] = layer_scale[0];
    ps->layer_scale[1] = layer_scale[1];
    ps->layer_scale[2] = layer_scale[2];
    ps->layer_rotation = layer_rotation;
    ps->parallax[0] = 0.0f;
    ps->parallax[1] = 0.0f;

    ps->draw(ctx);
}

void ParticleLayer::drawDebug(EngineContext& ctx) {
    if (!ps) return;

    float layer_origin[3] = {origin[0], origin[1], origin[2]};
    float layer_scale[3] = {scale[0], scale[1], scale[2]};
    float layer_rotation = rotation;
    if (scene_object_id != 0 && ctx.scene_tree) {
        if (const SceneTreeNode* node = ctx.scene_tree->find(scene_object_id)) {
            layer_scale[0] = node->scale[0];
            layer_scale[1] = node->scale[1];
            layer_scale[2] = node->scale[2];
            layer_rotation = node->angles[2];
        }
        ctx.scene_tree->worldPosition(scene_object_id, layer_origin);
    }

    const parallax_offset_t camera_offset = parallax_layer_offset(ctx, scene_object_id, layer_origin, parallax);
    ps->layer_origin[0] = layer_origin[0] + camera_offset.x;
    ps->layer_origin[1] = layer_origin[1] + camera_offset.y;
    ps->layer_origin[2] = layer_origin[2];
    ps->layer_scale[0] = layer_scale[0];
    ps->layer_scale[1] = layer_scale[1];
    ps->layer_scale[2] = layer_scale[2];
    ps->layer_rotation = layer_rotation;
    ps->parallax[0] = 0.0f;
    ps->parallax[1] = 0.0f;
    // A selected layer gets a readable bounding box by default. The CLI flags
    // explicitly select which overlays the global diagnostic pass draws.
    const bool cli_diagnostics = ctx.particle_debug_bounds || ctx.particle_debug_velocity;
    ps->show_bounds = cli_diagnostics ? ctx.particle_debug_bounds : true;
    ps->show_velocity = cli_diagnostics ? ctx.particle_debug_velocity : false;
    ps->drawDebugBounds(ctx);
}

bool ParticleLayer::requiresSceneColor() const {
    return ps && ps->requiresSceneColor();
}

void ParticleLayer::setSceneColorView(sg_view view) {
    if (ps) ps->setSceneColorView(view);
}
