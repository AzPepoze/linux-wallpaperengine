#include "particle_layer.h"

#include <cmath>

#include "particle_parser.h"
#include "particle_system.h"
#include "shared/core/engine_context.h"
#include "shared/core/utils.h"
#include "wallpaper/2d/camera/parallax.h"
#include "wallpaper/2d/tree/scene_tree.h"

ParticleLayer::ParticleLayer(const char* name, ParticleSystem* ps) : Layer(name), ps(ps) {}

ParticleLayer::~ParticleLayer() {
    if (ps) delete ps;
}

ParticleLayer* ParticleLayer::createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx) {
    const ParticleObjectConfig config = ParticleParser::parseObject(doc);
    if (config.particle_path.empty()) return nullptr;
    ParticleSystem* ps =
        ParticleSystem::createFromPath(config.particle_path.c_str(), ctx, ctx.scene.scene_w, ctx.scene.scene_h, config);
    if (ps) {
        ParticleLayer* layer = new ParticleLayer(config.name.c_str(), ps);
        layer->initFromDocument(doc, ctx);
        layer->path = ps->config_path;
        return layer;
    }
    return nullptr;
}

ParticlePlacement ParticleLayer::authoredPlacement(EngineContext& ctx) const {
    ParticlePlacement result;
    for (int axis = 0; axis < 3; ++axis) {
        result.origin[axis] = origin[axis];
        result.scale[axis] = scale[axis];
    }
    result.rotation_deg = rotation;
    if (scene_object_id != 0 && ctx.scene.scene_tree) {
        ScenePlacement placement;
        if (ctx.scene.scene_tree->worldPlacement(scene_object_id, placement)) {
            for (size_t axis = 0; axis < 3; ++axis) {
                result.origin[axis] = placement.origin[axis];
                result.scale[axis] = placement.scale[axis];
            }
            result.rotation_deg = placement.rotation_deg;
        }
    }
    return result;
}

void ParticleLayer::applyPlacement(EngineContext& ctx) {
    const ParticlePlacement placement = authoredPlacement(ctx);
    float origin_copy[3] = {placement.origin[0], placement.origin[1], placement.origin[2]};
    const parallax_offset_t camera_offset = parallax_layer_offset(ctx, scene_object_id, origin_copy, parallax);
    ps->setPlacement(placement, camera_offset.x, camera_offset.y);
}

void ParticleLayer::update(float dt, EngineContext& ctx) {
    if (!ps) return;
    applyPlacement(ctx);
    mat4x4 world;
    mat4x4_identity(world);
    if (ctx.scene.scene_tree) ctx.scene.scene_tree->worldTransform(scene_object_id, world);
    const float determinant = world[0][0] * world[1][1] - world[0][1] * world[1][0];
    if (ctx.input.mouse_position_valid && fabsf(determinant) > 1e-12f) {
        const float x = ctx.input.mouse_world_x - world[3][0];
        const float y = ctx.input.mouse_world_y - world[3][1];
        vec3 local = {(world[1][1] * x - world[1][0] * y) / determinant,
                      (world[0][0] * y - world[0][1] * x) / determinant, 0.0f};
        ps->updateControlPoints(local);
    }
    ps->update(dt);
}

void ParticleLayer::draw(EngineContext& ctx) {
    if (!ps) return;
    applyPlacement(ctx);

    float layer_origin[3] = {origin[0], origin[1], origin[2]};
    float layer_scale[3] = {scale[0], scale[1], scale[2]};
    float layer_rotation = rotation;
    if (scene_object_id != 0 && ctx.scene.scene_tree) {
        ScenePlacement placement;
        if (ctx.scene.scene_tree->worldPlacement(scene_object_id, placement)) {
            layer_scale[0] = placement.scale[0];
            layer_scale[1] = placement.scale[1];
            layer_scale[2] = placement.scale[2];
            layer_rotation = placement.rotation_deg;
            layer_origin[0] = placement.origin[0];
            layer_origin[1] = placement.origin[1];
            layer_origin[2] = placement.origin[2];
        }
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
    applyPlacement(ctx);

    float layer_origin[3] = {origin[0], origin[1], origin[2]};
    float layer_scale[3] = {scale[0], scale[1], scale[2]};
    float layer_rotation = rotation;
    if (scene_object_id != 0 && ctx.scene.scene_tree) {
        ScenePlacement placement;
        if (ctx.scene.scene_tree->worldPlacement(scene_object_id, placement)) {
            layer_scale[0] = placement.scale[0];
            layer_scale[1] = placement.scale[1];
            layer_scale[2] = placement.scale[2];
            layer_rotation = placement.rotation_deg;
            layer_origin[0] = placement.origin[0];
            layer_origin[1] = placement.origin[1];
            layer_origin[2] = placement.origin[2];
        }
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
    const bool cli_diagnostics = ctx.debug.particle_debug_bounds || ctx.debug.particle_debug_velocity;
    ps->show_bounds = cli_diagnostics ? ctx.debug.particle_debug_bounds : true;
    ps->show_velocity = cli_diagnostics ? ctx.debug.particle_debug_velocity : false;
    ps->drawDebugBounds(ctx);
}

bool ParticleLayer::requiresSceneColor() const {
    return ps && ps->requiresSceneColor();
}

void ParticleLayer::setSceneColorView(sg_view view) {
    if (ps) ps->setSceneColorView(view);
}
