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

void makePerspectiveCamera(mat4x4 projection, mat4x4 view, float scene_width, float scene_height,
                           float fov_override_degrees, float& camera_distance) {
    const float safe_height = std::max(scene_height, 1.0f);
    const float safe_width = std::max(scene_width, 1.0f);
    const float near_plane = 1.0f;
    const float far_plane = 100000.0f;
    camera_distance = 1000.0f;
    if (fov_override_degrees > 0.0f && fov_override_degrees < 179.0f) {
        camera_distance = safe_height / (2.0f * tanf(fov_override_degrees * (float)M_PI / 360.0f));
    }
    const float focal_length = camera_distance / (safe_height * 0.5f);

    mat4x4_identity(projection);
    projection[0][0] = focal_length * safe_height / safe_width;
    projection[1][1] = focal_length;
    projection[2][2] = -(far_plane + near_plane) / (far_plane - near_plane);
    projection[2][3] = -1.0f;
    projection[3][2] = -(2.0f * far_plane * near_plane) / (far_plane - near_plane);
    projection[3][3] = 0.0f;

    mat4x4_identity(view);
    mat4x4_translate_in_place(view, -safe_width * 0.5f, -safe_height * 0.5f, -camera_distance);
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
                // Wallpaper Engine stores particle size as a diameter; the shader
                // expands the quad around its centre using this half-extent.
                vertex.texcoord[3] = particle.size * 0.5f;
                vertex.color[0] = particle.color[0];
                vertex.color[1] = particle.color[1];
                vertex.color[2] = particle.color[2];
                vertex.color[3] = particle.alpha;
                float velocity_x = particle.velocity[0];
                float velocity_y = particle.velocity[1];
                // ComputeParticleTrailTangents normalizes the cross product.
                // Avoid feeding it a zero vector for incomplete/static presets.
                if (is_trail && velocity_x * velocity_x + velocity_y * velocity_y < 1e-8f) velocity_y = 0.001f;
                vertex.texcoord_c1[0] = velocity_x;
                vertex.texcoord_c1[1] = velocity_y;
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

            mat4x4 projection, view, view_projection, model, mvp;
            mat4x4_identity(model);
            float camera_distance = 1000.0f;
            if (use_perspective) {
                // Particle coordinates are Wallpaper Engine world coordinates (Y-up).
                // The focal length makes the z=0 reference plane match the scene extent.
                makePerspectiveCamera(projection, view, scene_w, scene_h, ctx.perspective_override_fov,
                                      camera_distance);
                mat4x4_translate_in_place(model, layer_origin[0] + parallax_x, layer_origin[1] + parallax_y,
                                          layer_origin[2]);
                mat4x4_rotate_Z(model, model, layer_rotation * (float)(M_PI / 180.0));
                mat4x4_scale_aniso(model, model, layer_scale[0], layer_scale[1], layer_scale[2]);
                mat4x4_mul(view_projection, projection, view);
            } else {
                mat4x4_ortho(projection, 0.0f, ctx.renderer.view_width, ctx.renderer.view_height, 0.0f, -1.0f,
                             1.0f);
                mat4x4_identity(view_projection);
                // Convert Wallpaper Engine's Y-up particle space exactly once at
                // the particle-to-screen boundary. Image layers remain untouched.
                mat4x4_translate_in_place(
                    model, ctx.offset_x + (layer_origin[0] + parallax_x) * ctx.render_scale,
                    ctx.offset_y + (scene_h - layer_origin[1] - parallax_y) * ctx.render_scale,
                    layer_origin[2] * ctx.render_scale);
                mat4x4_rotate_Z(model, model, -layer_rotation * (float)(M_PI / 180.0));
                mat4x4_scale_aniso(model, model, ctx.render_scale * layer_scale[0],
                                   -ctx.render_scale * layer_scale[1], layer_scale[2]);
                mat4x4_dup(view_projection, projection);
            }
            mat4x4_mul(mvp, view_projection, model);

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
            memcpy(particle_builtins.view_projection_matrix, view_projection, sizeof(mat4x4));
            fillVec4(particle_builtins.orientation_up, 0.0f, 1.0f, 0.0f);
            fillVec4(particle_builtins.orientation_right, 1.0f, 0.0f, 0.0f);
            fillVec4(particle_builtins.orientation_forward, 0.0f, 0.0f, 1.0f);
            fillVec4(particle_builtins.view_up, 0.0f, 1.0f, 0.0f);
            fillVec4(particle_builtins.view_right, 1.0f, 0.0f, 0.0f);
            fillVec4(particle_builtins.eye_position, scene_w * 0.5f, scene_h * 0.5f,
                     use_perspective ? camera_distance : 0.0f);

            const float frame_width = spritesheet_cols > 0 ? 1.0f / (float)spritesheet_cols : 0.0f;
            const float frame_height = spritesheet_rows > 0 ? 1.0f / (float)spritesheet_rows : 0.0f;
            float texture_ratio = texture_width > 0 ? (float)texture_height / (float)texture_width : 1.0f;
            if (frame_width > 0.0f && frame_height > 0.0f && texture_width > 0 && texture_height > 0) {
                texture_ratio = ((float)texture_height * frame_height) / ((float)texture_width * frame_width);
            }
            if (is_trail) {
                fillVec4(particle_builtins.render_var0, config.renderer.length, config.renderer.max_length, 0.0f,
                         (float)std::max(0, max_particles - 1));
            } else {
                fillVec4(particle_builtins.render_var0, 0.0f, 0.0f, 0.0f, 0.0f);
            }
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
                            (scene_h - layer_origin[1] - parallax_y - particle.position[1] * layer_scale[1]) *
                                ctx.render_scale;
            const float width = particle.size * layer_scale[0] * ctx.render_scale;
            const float height = particle.size * layer_scale[1] * ctx.render_scale;
            renderer_draw_sprite(ctx, &ctx.renderer, pass->pass_textures.texture0, pass->pass_textures.texture0_view,
                                 x - width * 0.5f, y - height * 0.5f, width, height, -particle.rotation, tint,
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
    if (show_bounds) {
        for (const ParticleEmitterConfig& emitter : config.emitters) {
            float color[4] = {1.0f, 1.0f, 0.0f, 1.0f};
            if (emitter.type == "boxrandom") {
                const float x = ctx.offset_x +
                                (layer_origin[0] + parallax_x + emitter.origin[0] - emitter.distance_max[0]) *
                                    ctx.render_scale;
                const float y =
                    ctx.offset_y +
                    (scene_h - layer_origin[1] - parallax_y - emitter.origin[1] - emitter.distance_max[1]) *
                        ctx.render_scale;
                renderer_draw_rect(&ctx.renderer, x, y, emitter.distance_max[0] * 2.0f * ctx.render_scale,
                                   emitter.distance_max[1] * 2.0f * ctx.render_scale, color);
            } else if (emitter.type == "sphererandom") {
                const float distance = emitter.distance_max[0];
                const float x = ctx.offset_x +
                                (layer_origin[0] + parallax_x + emitter.origin[0] - distance) * ctx.render_scale;
                const float y = ctx.offset_y +
                                (scene_h - layer_origin[1] - parallax_y - emitter.origin[1] - distance) *
                                    ctx.render_scale;
                renderer_draw_rect(&ctx.renderer, x, y, distance * 2.0f * ctx.render_scale,
                                   distance * 2.0f * ctx.render_scale, color);
            }
        }
    }
    float point_color[4] = {1, 0, 0, 1};
    float velocity_color[4] = {0, 1, 1, 1};
    for (const Particle& particle : particles) {
        const float x = ctx.offset_x +
                        (layer_origin[0] + parallax_x + particle.position[0] * layer_scale[0]) * ctx.render_scale;
        const float y = ctx.offset_y +
                        (scene_h - layer_origin[1] - parallax_y - particle.position[1] * layer_scale[1]) *
                            ctx.render_scale;
        if (show_bounds) {
            const float width = particle.size * 0.5f * layer_scale[0] * ctx.render_scale;
            const float height = particle.size * 0.5f * layer_scale[1] * ctx.render_scale;
            renderer_draw_rect(&ctx.renderer, x - width * 0.5f, y - height * 0.5f, width, height, point_color);
        }
        if (show_velocity) {
            const float velocity_scale = ctx.particle_debug_velocity_scale * ctx.render_scale;
            const float end_x = x + particle.velocity[0] * layer_scale[0] * velocity_scale;
            const float end_y = y - particle.velocity[1] * layer_scale[1] * velocity_scale;
            renderer_draw_line(&ctx.renderer, x, y, end_x, end_y, velocity_color);

            const float angle = atan2f(end_y - y, end_x - x);
            constexpr float arrow_length = 8.0f;
            constexpr float arrow_angle = 0.55f;
            renderer_draw_line(&ctx.renderer, end_x, end_y,
                               end_x - cosf(angle - arrow_angle) * arrow_length,
                               end_y - sinf(angle - arrow_angle) * arrow_length, velocity_color);
            renderer_draw_line(&ctx.renderer, end_x, end_y,
                               end_x - cosf(angle + arrow_angle) * arrow_length,
                               end_y - sinf(angle + arrow_angle) * arrow_length, velocity_color);
        }
    }
    for (ParticleSystem* child : children) child->drawDebugBounds(ctx);
}
