#include "particle_layer.h"

#include "core/context.h"
#include "core/engine_context.h"
#include "core/utils.h"
#include "wallpaper/scene/2d/parallax.h"
#include "wallpaper/scene/graph/scene_graph.h"

ParticleLayer::ParticleLayer(const char* name, ParticleSystem* ps) : Layer(name), ps(ps) {}

ParticleLayer::~ParticleLayer() {
    if (ps) delete ps;
}

ParticleLayer* ParticleLayer::createFromDocument(const wallpaper_engine::SceneObjectDocument& doc, EngineContext& ctx) {
    if (doc.particle.particle.empty()) return nullptr;
    ParticleSystem* ps =
        ParticleSystem::createFromPath(doc.particle.particle.c_str(), ctx.asset_mgr, ctx.scene_w, ctx.scene_h);
    if (ps) {
        ParticleLayer* layer = new ParticleLayer(doc.name.empty() ? "Particle" : doc.name.c_str(), ps);
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
    if (scene_object_id != 0 && ctx.scene_graph) {
        ctx.scene_graph->worldPosition(scene_object_id, layer_origin);
    }

    const parallax_offset_t camera_offset = parallax_layer_offset(ctx, scene_object_id, layer_origin, parallax);

    ps->layer_origin[0] = layer_origin[0] + camera_offset.x;
    ps->layer_origin[1] = layer_origin[1] + camera_offset.y;
    ps->layer_origin[2] = layer_origin[2];
    ps->parallax[0] = 0.0f;
    ps->parallax[1] = 0.0f;

    bool any_eff_solo = false;
    for (auto eff : effects) {
        if (eff->solo) {
            any_eff_solo = true;
            break;
        }
    }

    for (auto eff : effects) {
        if (!eff->visible || (any_eff_solo && !eff->solo)) continue;
        eff->apply(ctx);
    }
    ps->draw(ctx);
}

void ParticleLayer::drawDebug(EngineContext& ctx) {
    if (!ps) return;

    float layer_origin[3] = {origin[0], origin[1], origin[2]};
    if (scene_object_id != 0 && ctx.scene_graph) {
        ctx.scene_graph->worldPosition(scene_object_id, layer_origin);
    }

    const parallax_offset_t camera_offset = parallax_layer_offset(ctx, scene_object_id, layer_origin, parallax);
    ps->layer_origin[0] = layer_origin[0] + camera_offset.x;
    ps->layer_origin[1] = layer_origin[1] + camera_offset.y;
    ps->layer_origin[2] = layer_origin[2];
    ps->parallax[0] = 0.0f;
    ps->parallax[1] = 0.0f;
    ps->drawDebugBounds(ctx);
}
