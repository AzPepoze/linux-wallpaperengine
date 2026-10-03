#include "render.h"

#include <math.h>

#include <future>
#include <string>
#include <vector>

#include "render_internal.h"
#include "shader/shader_backend.h"
#include "shader/shader_compiler.h"
#include "shader/shader_processor.h"
#include "shared/core/context.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "sokol_glue.h"

using render_internal::kFirstWallpaperBlendMode;
using render_internal::kLastWallpaperBlendMode;

void renderer_draw_sprite(EngineContext& ctx, renderer_t* r, sg_image img, sg_view main_view, float x, float y, float w,
                          float h, float rotation, float tint[4], bool additive, const render_effect_pass_t* pass,
                          bool replace) {
    mat4x4 proj, model, mvp;
    mat4x4_ortho(proj, 0, r->view_width, r->view_height, 0, -1.0f, 1.0f);
    mat4x4_identity(model);
    mat4x4_translate_in_place(model, x, y, 0.0f);
    mat4x4_rotate_Z(model, model, rotation * (M_PI / 180.0f));
    mat4x4_scale_aniso(model, model, w, h, 1.0f);
    mat4x4_mul(mvp, proj, model);

    const sg_image_desc main_img_desc = sg_query_image_desc(img);
    for (int i = 0; i < SG_MAX_SAMPLER_BINDSLOTS; ++i) r->bind.samplers[i] = r->smp_repeat;
    if (main_img_desc.usage.color_attachment) {
        r->bind.samplers[0] = r->smp_clamp;
    }

    if (pass && pass->enabled && pass->pipeline.id != SG_INVALID_ID) {
        sg_apply_pipeline(pass->pipeline);
        r->bind.vertex_buffers[0] = pass->is_fullscreen_quad ? r->fullscreen_vertex_buffer : r->vertex_buffer;
        r->bind.samplers[0] =
            (!main_img_desc.usage.color_attachment && pass->repeat_effect_input) ? r->smp_repeat : r->smp_clamp;

        builtin_uniforms_t builtin = {};
        memcpy(builtin.mvp, mvp, sizeof(mat4x4));
        mat4x4_invert(builtin.mvp_inverse, mvp);
        builtin.parallax_pos[0] = ctx.parallax.smooth_x * 0.5f + 0.5f;
        builtin.parallax_pos[1] = ctx.parallax.smooth_y * 0.5f + 0.5f;
        builtin.time = ctx.time;
        builtin.screen_res[0] = r->view_width;
        builtin.screen_res[1] = r->view_height;
        builtin.texel_size[0] = r->view_width > 0.0f ? 1.0f / r->view_width : 0.0f;
        builtin.texel_size[1] = r->view_height > 0.0f ? 1.0f / r->view_height : 0.0f;
        builtin.pointer_position[0] = 0.5f;
        builtin.pointer_position[1] = 0.5f;
        if (ctx.input.mouse_position_valid && r->view_width > 0.0f && r->view_height > 0.0f) {
            builtin.pointer_position[0] = std::max(0.0f, std::min(1.0f, ctx.input.mouse_x / r->view_width));
            builtin.pointer_position[1] = std::max(0.0f, std::min(1.0f, ctx.input.mouse_y / r->view_height));
        }
        mat4x4_identity(builtin.effect_texture_projection);
        mat4x4_identity(builtin.effect_texture_projection_inverse);
        builtin.light_ambient_color[0] = ctx.scene.general.ambient_color[0];
        builtin.light_ambient_color[1] = ctx.scene.general.ambient_color[1];
        builtin.light_ambient_color[2] = ctx.scene.general.ambient_color[2];
        builtin.light_ambient_color[3] = 1.0f;
        builtin.light_skylight_color[0] = ctx.scene.general.skylight_color[0];
        builtin.light_skylight_color[1] = ctx.scene.general.skylight_color[1];
        builtin.light_skylight_color[2] = ctx.scene.general.skylight_color[2];
        builtin.light_skylight_color[3] = 1.0f;

        // Slot 0 is always the current effect input view.
        r->bind.views[0] = main_view;

        {
            sg_image_desc d = sg_query_image_desc(img);
            builtin.texture_resolutions[0][0] = d.width > 0 ? (float)d.width : 1.0f;
            builtin.texture_resolutions[0][1] = d.height > 0 ? (float)d.height : 1.0f;
            builtin.texture_resolutions[0][2] = builtin.texture_resolutions[0][0];
            builtin.texture_resolutions[0][3] = builtin.texture_resolutions[0][1];
        }

        const bool is_depth_parallax = pass->shader_name && strstr(pass->shader_name, "depthparallax") != nullptr;
        const bool is_waterwaves = pass->shader_name && strstr(pass->shader_name, "waterwaves") != nullptr;

        for (int i = 0; i < 11; i++) {
            int slot = i + 1;  // Shift by 1 because Slot 0 is the main view

            if (pass->override_views && i < (int)pass->num_override_views &&
                pass->override_views[i].id != SG_INVALID_ID) {
                r->bind.views[slot] = pass->override_views[i];
            } else if (pass->extra_views && i < (int)pass->num_extra_views &&
                       pass->extra_views[i].id != SG_INVALID_ID) {
                r->bind.views[slot] = pass->extra_views[i];
            } else if (i == 0) {
                // WE metadata: black for a missing depthparallax depth, full mask for waterwaves.
                if (is_waterwaves) {
                    r->bind.views[slot] = r->white_view;
                } else if (is_depth_parallax) {
                    r->bind.views[slot] = r->black_view;
                } else {
                    r->bind.views[slot] = r->black_view;
                }
            } else if (i == 1) {
                r->bind.views[slot] = r->white_view;  // Default to full mask for g_Texture2
            } else {
                r->bind.views[slot] = r->black_view;
            }

            if (slot < SG_MAX_SAMPLER_BINDSLOTS) {
                sg_image sampled_image = sg_query_view_image(r->bind.views[slot]);
                if (sampled_image.id != SG_INVALID_ID) {
                    sg_image_desc sampled_desc = sg_query_image_desc(sampled_image);
                    if (sampled_desc.usage.color_attachment) r->bind.samplers[slot] = r->smp_clamp;
                }
            }

            if (slot < 5) {
                sg_image target_img = sg_query_view_image(r->bind.views[slot]);
                if (target_img.id != SG_INVALID_ID) {
                    sg_image_desc d = sg_query_image_desc(target_img);
                    builtin.texture_resolutions[slot][0] = d.width > 0 ? (float)d.width : 1.0f;
                    builtin.texture_resolutions[slot][1] = d.height > 0 ? (float)d.height : 1.0f;
                    builtin.texture_resolutions[slot][2] = builtin.texture_resolutions[slot][0];
                    builtin.texture_resolutions[slot][3] = builtin.texture_resolutions[slot][1];
                } else {
                    builtin.texture_resolutions[slot][0] = 1.0f;
                    builtin.texture_resolutions[slot][1] = 1.0f;
                    builtin.texture_resolutions[slot][2] = 1.0f;
                    builtin.texture_resolutions[slot][3] = 1.0f;
                }
            }
        }

        sg_range b_range = SG_RANGE(builtin.mvp);
        sg_apply_uniforms(0, &b_range);
        constexpr size_t kBuiltinRestSize = sizeof(builtin_uniforms_t) - sizeof(mat4x4);
        const uint8_t* builtin_rest = reinterpret_cast<const uint8_t*>(&builtin) + sizeof(mat4x4);
        sg_range res_range = {.ptr = builtin_rest, .size = kBuiltinRestSize};
        sg_apply_uniforms(1, &res_range);

        alignas(16) uint8_t fragment_uniforms[kBuiltinRestSize + sizeof(float) * 4] = {};
        memcpy(fragment_uniforms, builtin_rest, kBuiltinRestSize);
        memcpy(fragment_uniforms + kBuiltinRestSize, tint, sizeof(float) * 4);
        sg_range fragment_range = {.ptr = fragment_uniforms, .size = sizeof(fragment_uniforms)};
        sg_apply_uniforms(2, &fragment_range);
    } else {
        sg_pipeline target_pipeline = replace ? r->pip_copy : (additive ? r->pip_add : r->pip_alpha);
        if (target_pipeline.id == SG_INVALID_ID) {
            LOG_TAG_E("RENDER", "renderer_draw_sprite: default sprite pipeline %s is invalid (id=0)!",
                      replace ? "pip_copy" : (additive ? "pip_add" : "pip_alpha"));
            return;
        }
        r->bind.views[0] = main_view;
        for (int i = 1; i < 12; i++) {
            r->bind.views[i] = r->black_view;
        }
        sg_apply_pipeline(target_pipeline);

        sg_range mvp_range = SG_RANGE(mvp);
        sg_apply_uniforms(0, &mvp_range);
        sg_range tint_range = {.ptr = tint, .size = sizeof(float) * 4};
        sg_apply_uniforms(1, &tint_range);
    }

    sg_apply_bindings(&r->bind);
    if (pass && pass->enabled && pass->pipeline.id != SG_INVALID_ID) {
        if (pass->apply_custom_uniforms) {
            pass->apply_custom_uniforms(pass->user_data);
        }
    }

    sg_draw(0, 6, 1);
    r->draw_calls++;

    for (int i = 0; i < 12; i++) {
        r->bind.views[i] = (sg_view){SG_INVALID_ID};
    }
    r->bind.vertex_buffers[0] = r->vertex_buffer;
    r->bind.index_buffer = r->index_buffer;
}

void renderer_draw_unpremultiplied(renderer_t* r, sg_view source_view, float width, float height) {
    if (r->pip_unpremul.id == SG_INVALID_ID || source_view.id == SG_INVALID_ID) return;
    mat4x4 proj, model, mvp;
    mat4x4_ortho(proj, 0, width, height, 0, -1.0f, 1.0f);
    mat4x4_identity(model);
    mat4x4_scale_aniso(model, model, width, height, 1.0f);
    mat4x4_mul(mvp, proj, model);

    for (int i = 0; i < SG_MAX_SAMPLER_BINDSLOTS; ++i) r->bind.samplers[i] = r->smp_clamp;
    r->bind.views[0] = source_view;
    for (int i = 1; i < 12; ++i) r->bind.views[i] = r->black_view;
    sg_apply_pipeline(r->pip_unpremul);
    sg_range mvp_range = SG_RANGE(mvp);
    sg_apply_uniforms(0, &mvp_range);
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    sg_range tint_range = {.ptr = white, .size = sizeof(white)};
    sg_apply_uniforms(1, &tint_range);
    sg_apply_bindings(&r->bind);
    sg_draw(0, 6, 1);
    r->draw_calls++;

    for (int i = 0; i < 12; i++) r->bind.views[i] = (sg_view){SG_INVALID_ID};
    r->bind.vertex_buffers[0] = r->vertex_buffer;
    r->bind.index_buffer = r->index_buffer;
}

void renderer_draw_image_composite(EngineContext& ctx, renderer_t* r, sg_image image, sg_view image_view,
                                   sg_view scene_view, float x, float y, float width, float height, float rotation,
                                   float tint[4], int blend_mode) {
    if (blend_mode == 0) {
        renderer_draw_sprite(ctx, r, image, image_view, x, y, width, height, rotation, tint, false, nullptr);
        return;
    }

    if (blend_mode == 31) {
        renderer_draw_sprite(ctx, r, image, image_view, x, y, width, height, rotation, tint, true, nullptr);
        return;
    }

    if (blend_mode < kFirstWallpaperBlendMode || blend_mode > kLastWallpaperBlendMode) return;

    if (r->pip_image_composite[blend_mode].id == SG_INVALID_ID) return;

    static int logged_composite_frames = 0;
    if (logged_composite_frames < 10) {
        logged_composite_frames++;
        LOG_TAG_I("RENDER",
                  "renderer_draw_image_composite: blend=%d, img=%u, view=%u, scene_view=%u, rect=(%.1f, %.1f, %.1f, "
                  "%.1f), tint=(%.2f, %.2f, %.2f, %.2f)",
                  blend_mode, image.id, image_view.id, scene_view.id, x, y, width, height, tint[0], tint[1], tint[2],
                  tint[3]);
    }

    sg_view background[] = {scene_view};
    render_effect_pass_t composite = {};
    composite.enabled = true;
    composite.pipeline = r->pip_image_composite[blend_mode];
    composite.shader_name = "image-composite";
    composite.override_views = background;
    composite.num_override_views = 1;

    renderer_draw_sprite(ctx, r, image, image_view, x, y, width, height, rotation, tint, false, &composite);
}

void renderer_draw_rect(renderer_t* r, float x, float y, float w, float h, float color[4]) {
    renderer_draw_line(r, x, y, x + w, y, color);
    renderer_draw_line(r, x + w, y, x + w, y + h, color);
    renderer_draw_line(r, x + w, y + h, x, y + h, color);
    renderer_draw_line(r, x, y + h, x, y, color);
}

void renderer_draw_line(renderer_t* r, float x0, float y0, float x1, float y1, float color[4]) {
    r->bind.vertex_buffers[0] = r->vertex_buffer;
    r->bind.index_buffer = r->index_buffer;
    r->bind.views[0] = r->white_view;
    for (int i = 1; i < 12; i++) r->bind.views[i] = r->black_view;

    sg_apply_pipeline(r->pip_lines);
    sg_apply_bindings(&r->bind);

    mat4x4 proj, model, mvp;
    mat4x4_ortho(proj, 0, r->view_width, r->view_height, 0, -1.0f, 1.0f);
    mat4x4_identity(model);
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    mat4x4_translate_in_place(model, x0, y0, 0.0f);
    mat4x4_rotate_Z(model, model, atan2f(dy, dx));
    mat4x4_scale_aniso(model, model, sqrtf(dx * dx + dy * dy), 1.0f, 1.0f);
    mat4x4_mul(mvp, proj, model);
    sg_range mvp_range = SG_RANGE(mvp);
    sg_apply_uniforms(0, &mvp_range);
    sg_range tint_range = {.ptr = color, .size = sizeof(float) * 4};
    sg_apply_uniforms(1, &tint_range);
    sg_draw(0, 6, 1);
    r->draw_calls++;

    for (int i = 0; i < 12; i++) r->bind.views[i] = (sg_view){SG_INVALID_ID};
    r->bind.vertex_buffers[0] = r->vertex_buffer;
    r->bind.index_buffer = r->index_buffer;
}
