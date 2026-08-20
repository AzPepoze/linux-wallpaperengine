#include "particle_layer.h"

#include "../../core/context.h"
#include "../../core/engine_context.h"
#include "../../core/utils.h"
#include "../parallax.h"

ParticleLayer::ParticleLayer(const char* name, ParticleSystem* ps) : Layer(name), ps(ps) {}

ParticleLayer::~ParticleLayer() {
    if (ps) delete ps;
}

ParticleLayer* ParticleLayer::createFromJSON(cJSON* node, EngineContext& ctx) {
    ParticleSystem* ps = ParticleSystem::createFromJSON(node, ctx.asset_mgr, ctx.scene_w, ctx.scene_h);
    if (ps) {
        ParticleLayer* layer = new ParticleLayer("Particle", ps);
        layer->loadBaseProperties(node, ctx);
        layer->path = ps->config_path;
        return layer;
    }
    return nullptr;
}

void ParticleLayer::update(float dt, EngineContext& ctx) {
    if (ps) ps->update(dt);
}

void ParticleLayer::draw(EngineContext& ctx) {
    if (!ps) return;

    const parallax_offset_t camera_offset = parallax_layer_offset(ctx, origin, parallax);

    // Normal camera parallax belongs to the layer transform. Feed particles the
    // translated layer origin and keep ParticleSystem's old local parallax path disabled.
    ps->layer_origin[0] = origin[0] + camera_offset.x;
    ps->layer_origin[1] = origin[1] + camera_offset.y;
    ps->layer_origin[2] = origin[2];
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
        if (any_eff_solo) {
            if (eff->solo) eff->apply(ctx);
        } else {
            if (eff->visible) eff->apply(ctx);
        }
    }
    ps->draw(ctx);
}

void ParticleLayer::drawDebug(EngineContext& ctx) {
    if (!ps) return;

    const parallax_offset_t camera_offset = parallax_layer_offset(ctx, origin, parallax);
    ps->layer_origin[0] = origin[0] + camera_offset.x;
    ps->layer_origin[1] = origin[1] + camera_offset.y;
    ps->layer_origin[2] = origin[2];
    ps->parallax[0] = 0.0f;
    ps->parallax[1] = 0.0f;
    ps->drawDebugBounds(ctx);
}
