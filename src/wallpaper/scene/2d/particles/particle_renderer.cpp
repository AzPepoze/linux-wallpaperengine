#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/config.h"
#include "core/engine_context.h"
#include "particle_system.h"
#include "render/render.h"
#include "render/shader/shader_compiler.h"
#include "wallpaper/scene/2d/effects/effect.h"

namespace {

struct ParticleVertex {
    float position[3];
    float texcoord[4];
    float color[4];
    float texcoord_c1[4];
    float texcoord_c2[2];
};

static_assert(sizeof(ParticleVertex) == sizeof(float) * 17, "Wallpaper Engine particle vertex layout changed");

void fillVec4(vec4 value, float x, float y, float z, float w = 0.0f) {
    value[0] = x;
    value[1] = y;
    value[2] = z;
    value[3] = w;
}

}  // namespace

void ParticleSystem::draw(EngineContext& ctx) {
    ShaderPass* pass = material_pass;
    const bool material_ready = pass && pass->pass_textures.texture0.id != SG_INVALID_ID &&
                                pass->pass_textures.texture0_view.id != SG_INVALID_ID;

    if (material_ready && !particles.empty() && pass->compiled.pipeline.id != SG_INVALID_ID &&
        pass->compiled.vertex_layout == ShaderVertexLayout::ParticleSprite &&
        particle_vertex_buffer.id != SG_INVALID_ID && particle_index_buffer.id != SG_INVALID_ID) {
        std::vector<ParticleVertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(particles.size() * 4);
        indices.reserve(particles.size() * 6);

        for (const Particle& particle : particles) {
            if (!std::isfinite(particle.position[0]) || !std::isfinite(particle.position[1]) ||
                !std::isfinite(particle.position[2]) || !std::isfinite(particle.size) || particle.size <= 0.0f)
                continue;

            const float age = particle.max_life - particle.life;
            float lifetime = particle.max_life > 0.0f ? std::clamp(age / particle.max_life, 0.0f, 1.0f) : 0.0f;
            if (spritesheet_frames > 1 && particle.frame >= 0.0f) {
                if (config.animation_mode == "randomframe")
                    lifetime = (particle.frame + 0.5f) / (float)spritesheet_frames;
                else
                    lifetime = particle.frame / (float)spritesheet_frames;
            }

            const uint32_t base = (uint32_t)vertices.size();
            auto add_vertex = [&](float u, float v) {
                ParticleVertex vertex = {};
                vertex.position[0] = particle.position[0];
                vertex.position[1] = particle.position[1];
                vertex.position[2] = particle.position[2];
                vertex.texcoord[0] = u;
                vertex.texcoord[1] = v;
                vertex.texcoord[2] = particle.rotation;
                vertex.texcoord[3] = particle.size;
                vertex.color[0] = particle.color[0];
                vertex.color[1] = particle.color[1];
                vertex.color[2] = particle.color[2];
                vertex.color[3] = particle.alpha;
                vertex.texcoord_c1[0] = particle.velocity[0];
                vertex.texcoord_c1[1] = particle.velocity[1];
                vertex.texcoord_c1[2] = particle.velocity[2];
                vertex.texcoord_c1[3] = lifetime;
                vertex.texcoord_c2[0] = 0.0f;
                vertex.texcoord_c2[1] = 0.0f;
                vertices.push_back(vertex);
            };

            add_vertex(0.0f, 1.0f);
            add_vertex(1.0f, 1.0f);
            add_vertex(1.0f, 0.0f);
            add_vertex(0.0f, 0.0f);
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
            indices.push_back(base + 0);
        }

        if (!vertices.empty() && !indices.empty()) {
            sg_range vertex_range = {.ptr = vertices.data(), .size = vertices.size() * sizeof(ParticleVertex)};
            sg_range index_range = {.ptr = indices.data(), .size = indices.size() * sizeof(uint32_t)};
            sg_update_buffer(particle_vertex_buffer, &vertex_range);
            sg_update_buffer(particle_index_buffer, &index_range);

            const float parallax_x = parallax[0] * ctx.parallax_smooth_x * Config::kParallaxScale;
            const float parallax_y = parallax[1] * ctx.parallax_smooth_y * Config::kParallaxScale;

            mat4x4 projection, model, mvp;
            mat4x4_ortho(projection, 0.0f, ctx.renderer.view_width, ctx.renderer.view_height, 0.0f, -1.0f, 1.0f);
            mat4x4_identity(model);
            mat4x4_translate_in_place(model, ctx.offset_x + (layer_origin[0] + parallax_x) * ctx.render_scale,
                                      ctx.offset_y + (layer_origin[1] + parallax_y) * ctx.render_scale,
                                      layer_origin[2] * ctx.render_scale);
            mat4x4_rotate_Z(model, model, layer_rotation * (float)(M_PI / 180.0));
            mat4x4_scale_aniso(model, model, ctx.render_scale * layer_scale[0], ctx.render_scale * layer_scale[1],
                               layer_scale[2]);
            mat4x4_mul(mvp, projection, model);

            builtin_uniforms_t builtins = {};
            memcpy(builtins.mvp, mvp, sizeof(mat4x4));
            mat4x4_invert(builtins.mvp_inverse, mvp);
            builtins.parallax_pos[0] = ctx.parallax_smooth_x * 0.5f + 0.5f;
            builtins.parallax_pos[1] = ctx.parallax_smooth_y * 0.5f + 0.5f;
            builtins.time = ctx.time;
            builtins.screen_res[0] = ctx.renderer.view_width;
            builtins.screen_res[1] = ctx.renderer.view_height;
            builtins.texel_size[0] = ctx.renderer.view_width > 0.0f ? 1.0f / ctx.renderer.view_width : 0.0f;
            builtins.texel_size[1] = ctx.renderer.view_height > 0.0f ? 1.0f / ctx.renderer.view_height : 0.0f;
            builtins.pointer_position[0] = 0.5f;
            builtins.pointer_position[1] = 0.5f;
            if (ctx.mouse_position_valid && ctx.renderer.view_width > 0.0f && ctx.renderer.view_height > 0.0f) {
                builtins.pointer_position[0] = std::clamp(ctx.mouse_x / ctx.renderer.view_width, 0.0f, 1.0f);
                builtins.pointer_position[1] = std::clamp(ctx.mouse_y / ctx.renderer.view_height, 0.0f, 1.0f);
            }
            mat4x4_identity(builtins.effect_texture_projection);
            mat4x4_identity(builtins.effect_texture_projection_inverse);

            particle_builtin_uniforms_t particle_builtins = {};
            memcpy(particle_builtins.model_matrix, model, sizeof(mat4x4));
            mat4x4_invert(particle_builtins.model_matrix_inverse, model);
            memcpy(particle_builtins.view_projection_matrix, projection, sizeof(mat4x4));
            fillVec4(particle_builtins.orientation_up, 0.0f, 1.0f, 0.0f);
            fillVec4(particle_builtins.orientation_right, 1.0f, 0.0f, 0.0f);
            fillVec4(particle_builtins.orientation_forward, 0.0f, 0.0f, 1.0f);
            fillVec4(particle_builtins.view_up, 0.0f, 1.0f, 0.0f);
            fillVec4(particle_builtins.view_right, 1.0f, 0.0f, 0.0f);
            fillVec4(particle_builtins.eye_position, 0.0f, 0.0f, 1000.0f);

            const float frame_width = spritesheet_cols > 0 ? 1.0f / (float)spritesheet_cols : 0.0f;
            const float frame_height = spritesheet_rows > 0 ? 1.0f / (float)spritesheet_rows : 0.0f;
            float texture_ratio = texture_width > 0 ? (float)texture_height / (float)texture_width : 1.0f;
            if (frame_width > 0.0f && frame_height > 0.0f && texture_width > 0 && texture_height > 0) {
                texture_ratio = ((float)texture_height * frame_height) / ((float)texture_width * frame_width);
            }
            fillVec4(particle_builtins.render_var0, 0.0f, 0.0f, 0.0f, 0.0f);
            fillVec4(particle_builtins.render_var1, frame_width, frame_height, (float)spritesheet_frames, texture_ratio);

            render_effect_pass_t render_pass = pass->getRenderPass();
            sg_view overrides[11] = {};
            if (has_refract && scene_color_view.id != SG_INVALID_ID) overrides[2] = scene_color_view;
            render_pass.override_views = overrides;
            render_pass.num_override_views = 11;

            renderer_draw_particle_batch(ctx, &ctx.renderer, particle_vertex_buffer, particle_index_buffer,
                                         (int)indices.size(), pass->pass_textures.texture0,
                                         pass->pass_textures.texture0_view, &render_pass, builtins, particle_builtins);
        }
    } else if (material_ready) {
        // Compatibility fallback for simple materials whose authored shader cannot yet use
        // the Wallpaper Engine particle vertex interface.
        const float parallax_x = parallax[0] * ctx.parallax_smooth_x * Config::kParallaxScale;
        const float parallax_y = parallax[1] * ctx.parallax_smooth_y * Config::kParallaxScale;
        for (const Particle& particle : particles) {
            float tint[4] = {particle.color[0], particle.color[1], particle.color[2], particle.alpha};
            const float x = ctx.offset_x +
                            (layer_origin[0] + parallax_x + particle.position[0] * layer_scale[0]) * ctx.render_scale;
            const float y = ctx.offset_y +
                            (layer_origin[1] + parallax_y + particle.position[1] * layer_scale[1]) * ctx.render_scale;
            const float width = particle.size * layer_scale[0] * ctx.render_scale;
            const float height = particle.size * layer_scale[1] * ctx.render_scale;
            renderer_draw_sprite(ctx, &ctx.renderer, pass->pass_textures.texture0, pass->pass_textures.texture0_view,
                                 x - width * 0.5f, y - height * 0.5f, width, height, particle.rotation, tint,
                                 is_additive, nullptr);
        }
    }

    for (ParticleSystem* child : children) {
        child->layer_origin[0] = layer_origin[0];
        child->layer_origin[1] = layer_origin[1];
        child->layer_origin[2] = layer_origin[2];
        child->layer_scale[0] = layer_scale[0];
        child->layer_scale[1] = layer_scale[1];
        child->layer_scale[2] = layer_scale[2];
        child->layer_rotation = layer_rotation;
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
            const float x = ctx.offset_x +
                            (layer_origin[0] + parallax_x + emitter.origin[0] - emitter.distance_max[0]) *
                                ctx.render_scale;
            const float y = ctx.offset_y +
                            (layer_origin[1] + parallax_y + emitter.origin[1] - emitter.distance_max[1]) *
                                ctx.render_scale;
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
        const float x = ctx.offset_x +
                        (layer_origin[0] + parallax_x + particle.position[0] * layer_scale[0]) * ctx.render_scale;
        const float y = ctx.offset_y +
                        (layer_origin[1] + parallax_y + particle.position[1] * layer_scale[1]) * ctx.render_scale;
        const float width = particle.size * layer_scale[0] * ctx.render_scale;
        const float height = particle.size * layer_scale[1] * ctx.render_scale;
        renderer_draw_rect(&ctx.renderer, x - width * 0.5f, y - height * 0.5f, width, height, color);
    }
    for (ParticleSystem* child : children) child->drawDebugBounds(ctx);
}
