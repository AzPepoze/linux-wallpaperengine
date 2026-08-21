#include "core/config.h"
#include "core/engine_context.h"
#include "particle_system.h"
#include "render/render.h"

void ParticleSystem::draw(EngineContext& ctx) {
    if (texture.id == SG_INVALID_ID) return;
    if (cached_view.id == SG_INVALID_ID) {
        sg_view_desc view_desc = {};
        view_desc.texture.image = texture;
        cached_view = sg_make_view(&view_desc);
    }
    const float parallax_x = parallax[0] * ctx.parallax_smooth_x * Config::kParallaxScale;
    const float parallax_y = parallax[1] * ctx.parallax_smooth_y * Config::kParallaxScale;
    for (const Particle& particle : particles) {
        float tint[4] = {particle.color[0], particle.color[1], particle.color[2], particle.alpha};
        const float x = ctx.offset_x + (layer_origin[0] + parallax_x + particle.position[0]) * ctx.render_scale;
        const float y = ctx.offset_y + (layer_origin[1] + parallax_y + particle.position[1]) * ctx.render_scale;
        const float size = particle.size * ctx.render_scale;
        renderer_draw_sprite(ctx, &ctx.renderer, texture, cached_view, x - size * 0.5f, y - size * 0.5f, size, size,
                             particle.rotation, tint, is_additive, nullptr);
    }
    for (ParticleSystem* child : children) {
        child->layer_origin[0] = layer_origin[0];
        child->layer_origin[1] = layer_origin[1];
        child->parallax[0] = parallax[0];
        child->parallax[1] = parallax[1];
        child->draw(ctx);
    }
}

void ParticleSystem::drawDebugBounds(EngineContext& ctx) {
    const float parallax_x = parallax[0] * ctx.parallax_smooth_x * Config::kParallaxScale;
    const float parallax_y = parallax[1] * ctx.parallax_smooth_y * Config::kParallaxScale;
    for (const ParticleEmitterConfig& emitter : config.emitters) {
        float color[4] = {1.0f, 1.0f, 0.0f, 1.0f};
        if (emitter.type == "boxrandom") {
            const float x =
                ctx.offset_x +
                (layer_origin[0] + parallax_x + emitter.origin[0] - emitter.distance_max[0]) * ctx.render_scale;
            const float y =
                ctx.offset_y +
                (layer_origin[1] + parallax_y + emitter.origin[1] - emitter.distance_max[1]) * ctx.render_scale;
            renderer_draw_rect(&ctx.renderer, x, y, emitter.distance_max[0] * 2.0f * ctx.render_scale,
                               emitter.distance_max[1] * 2.0f * ctx.render_scale, color);
        } else if (emitter.type == "sphererandom") {
            const float distance = emitter.distance_max[0];
            const float x =
                ctx.offset_x + (layer_origin[0] + parallax_x + emitter.origin[0] - distance) * ctx.render_scale;
            const float y =
                ctx.offset_y + (layer_origin[1] + parallax_y + emitter.origin[1] - distance) * ctx.render_scale;
            renderer_draw_rect(&ctx.renderer, x, y, distance * 2.0f * ctx.render_scale,
                               distance * 2.0f * ctx.render_scale, color);
        }
    }
    float color[4] = {1, 0, 0, 1};
    for (const Particle& particle : particles) {
        const float x = ctx.offset_x + (layer_origin[0] + parallax_x + particle.position[0]) * ctx.render_scale;
        const float y = ctx.offset_y + (layer_origin[1] + parallax_y + particle.position[1]) * ctx.render_scale;
        const float size = particle.size * ctx.render_scale;
        renderer_draw_rect(&ctx.renderer, x - size * 0.5f, y - size * 0.5f, size, size, color);
    }
    for (ParticleSystem* child : children) child->drawDebugBounds(ctx);
}
