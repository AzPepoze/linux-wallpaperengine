#include "render.h"

#include <math.h>

#include <string>

#include "../../libs/sokol/sokol_glue.h"
#include "../core/context.h"
#include "../core/engine_context.h"
#include "../core/logger.h"
#include "effect.h"
#include "shader_backend.h"

void renderer_init(renderer_t* r, float w, float h) {
    r->view_width = w;
    r->view_height = h;

    vertex_t vertices[] = {
        {0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}};
    sg_buffer_desc v_desc = {};
    v_desc.data = SG_RANGE(vertices);
    r->vertex_buffer = sg_make_buffer(&v_desc);
    r->bind.vertex_buffers[0] = r->vertex_buffer;

    uint16_t indices[] = {0, 1, 2, 0, 2, 3};
    sg_buffer_desc i_desc = {};
    i_desc.usage.index_buffer = true;
    i_desc.data = SG_RANGE(indices);
    r->index_buffer = sg_make_buffer(&i_desc);
    r->bind.index_buffer = r->index_buffer;

    sg_sampler_desc s_desc = {};
    s_desc.min_filter = SG_FILTER_LINEAR;
    s_desc.mag_filter = SG_FILTER_LINEAR;
    s_desc.wrap_u = SG_WRAP_REPEAT;
    s_desc.wrap_v = SG_WRAP_REPEAT;
    r->smp = sg_make_sampler(&s_desc);
    for (int i = 0; i < SG_MAX_SAMPLER_BINDSLOTS; i++) {
        r->bind.samplers[i] = r->smp;
    }

    uint32_t pixel = 0xFFFFFFFF;
    sg_image_desc img_desc = {};
    img_desc.width = 1;
    img_desc.height = 1;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.data.mip_levels[0] = {&pixel, 4};
    r->white_pixel = sg_make_image(&img_desc);

    sg_view_desc wv_desc = {};
    wv_desc.texture.image = r->white_pixel;
    r->white_view = sg_make_view(&wv_desc);

    pixel = 0x00000000;
    r->black_pixel = sg_make_image(&img_desc);

    sg_view_desc bv_desc = {};
    bv_desc.texture.image = r->black_pixel;
    r->black_view = sg_make_view(&bv_desc);

    pixel = 0x808080FF;  // 50% gray = neutral depth
    r->gray_pixel = sg_make_image(&img_desc);

    sg_view_desc gv_desc = {};
    gv_desc.texture.image = r->gray_pixel;
    r->gray_view = sg_make_view(&gv_desc);

    const std::string vertex_source =
        "#version 330\n"
        "uniform mat4 mvp;\n"
        "layout(location=0) in vec2 position;\n"
        "layout(location=1) in vec2 texcoord0;\n"
        "out vec2 uv;\n"
        "void main() {\n"
        "  gl_Position = mvp * vec4(position, 0.0, 1.0);\n"
        "  uv = texcoord0;\n"
        "}\n";
    const std::string fragment_source =
        "#version 330\n"
        "precision mediump float;\n"
        "uniform sampler2D tex;\n"
        "uniform vec4 tint;\n"
        "in vec2 uv;\n"
        "out vec4 frag_color;\n"
        "void main() {\n"
        "  frag_color = texture(tex, uv) * tint;\n"
        "}\n";

    sg_shader_desc shd_desc = {};
    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(mat4x4);
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;

    shd_desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.uniform_blocks[1].size = sizeof(float) * 4;
    shd_desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "tint";
    shd_desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;

    shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[0].glsl_name = "tex";
    shd_desc.texture_sampler_pairs[0].view_slot = 0;
    shd_desc.texture_sampler_pairs[0].sampler_slot = 0;

    sg_shader shd = create_backend_shader(&shd_desc, vertex_source, fragment_source, "renderer-default");

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.index_type = SG_INDEXTYPE_UINT16;
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    r->pip_alpha = sg_make_pipeline(&pip_desc);

    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE;
    r->pip_add = sg_make_pipeline(&pip_desc);

    // Line Pipeline
    pip_desc.primitive_type = SG_PRIMITIVETYPE_LINES;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    r->pip_lines = sg_make_pipeline(&pip_desc);
}

void renderer_update_viewport(renderer_t* r, float w, float h) {
    r->view_width = w;
    r->view_height = h;
}

void renderer_draw_sprite(EngineContext& ctx, renderer_t* r, sg_image img, sg_view main_view, float x, float y, float w,
                          float h, float rotation, float tint[4], bool additive, ShaderPass* pass) {
    mat4x4 proj, model, mvp;
    mat4x4_ortho(proj, 0, r->view_width, r->view_height, 0, -1.0f, 1.0f);
    mat4x4_identity(model);
    mat4x4_translate_in_place(model, x, y, 0.0f);
    mat4x4_rotate_Z(model, model, rotation * (M_PI / 180.0f));
    mat4x4_scale_aniso(model, model, w, h, 1.0f);
    mat4x4_mul(mvp, proj, model);

    if (pass && pass->enabled && pass->compiled.pipeline.id != SG_INVALID_ID) {
        sg_apply_pipeline(pass->compiled.pipeline);

        // Built-in Uniforms Setup
        builtin_uniforms_t builtin = {};
        memcpy(builtin.mvp, mvp, sizeof(mat4x4));
        builtin.parallax_pos[0] = ctx.parallax_smooth_x * 0.5f + 0.5f;
        builtin.parallax_pos[1] = ctx.parallax_smooth_y * 0.5f + 0.5f;
        builtin.time = ctx.time;
        builtin.screen_res[0] = r->view_width;
        builtin.screen_res[1] = r->view_height;
        mat4x4_identity(builtin.effect_texture_projection);
        mat4x4_identity(builtin.effect_texture_projection_inverse);

        // Slot 0 (g_Texture0) is ALWAYS the current effect input view.
        r->bind.views[0] = main_view;

        // Setup Main Image Resolution (Slot 0)
        {
            sg_image_desc d = sg_query_image_desc(img);
            builtin.texture_resolutions[0][0] = d.width > 0 ? (float)d.width : 1.0f;
            builtin.texture_resolutions[0][1] = d.height > 0 ? (float)d.height : 1.0f;
            builtin.texture_resolutions[0][2] = builtin.texture_resolutions[0][0];
            builtin.texture_resolutions[0][3] = builtin.texture_resolutions[0][1];
        }

        const bool is_waterwaves = pass->shader_name.find("waterwaves") != std::string::npos;

        // Slot 1+ (Extra Textures from pass->pass_textures.cached_views)
        for (int i = 0; i < 11; i++) {
            int slot = i + 1;  // Shift by 1 because Slot 0 is the main view

            if (i < (int)pass->pass_textures.cached_views.size() &&
                pass->pass_textures.cached_views[i].id != SG_INVALID_ID) {
                r->bind.views[slot] = pass->pass_textures.cached_views[i];
            } else if (i == 0) {
                // g_Texture1 is depth for depthparallax, but a mask for waterwaves.
                r->bind.views[slot] = is_waterwaves ? (sg_view)r->white_view : (sg_view)r->gray_view;
            } else if (i == 1) {
                r->bind.views[slot] = r->white_view;  // Default to full mask for g_Texture2
            } else {
                r->bind.views[slot] = r->black_view;
            }

            // Resolution indices for extra textures (Slot 1..4)
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
        sg_range res_range = {.ptr = builtin.texture_resolutions, .size = sizeof(builtin_uniforms_t) - sizeof(mat4x4)};
        sg_apply_uniforms(1, &res_range);

        constexpr size_t kBuiltinRestSize = sizeof(builtin_uniforms_t) - sizeof(mat4x4);
        alignas(16) uint8_t fragment_uniforms[kBuiltinRestSize + sizeof(float) * 4] = {};
        memcpy(fragment_uniforms, builtin.texture_resolutions, kBuiltinRestSize);
        memcpy(fragment_uniforms + kBuiltinRestSize, tint, sizeof(float) * 4);
        sg_range fragment_range = {.ptr = fragment_uniforms, .size = sizeof(fragment_uniforms)};
        sg_apply_uniforms(2, &fragment_range);
    } else {
        r->bind.views[0] = main_view;
        for (int i = 1; i < 12; i++) {
            r->bind.views[i] = r->black_view;
        }
        sg_apply_pipeline(additive ? r->pip_add : r->pip_alpha);

        sg_range mvp_range = SG_RANGE(mvp);
        sg_apply_uniforms(0, &mvp_range);
        sg_range tint_range = {.ptr = tint, .size = sizeof(float) * 4};
        sg_apply_uniforms(1, &tint_range);
    }

    sg_apply_bindings(&r->bind);
    if (pass && pass->enabled && pass->compiled.pipeline.id != SG_INVALID_ID) {
        pass->applyUniforms();
    }

    sg_draw(0, 6, 1);
    r->draw_calls++;

    // Clean up bindings for next call
    for (int i = 0; i < 12; i++) {
        r->bind.views[i] = (sg_view){SG_INVALID_ID};
    }
}

void renderer_draw_rect(renderer_t* r, float x, float y, float w, float h, float color[4]) {
    renderer_draw_line(r, x, y, x + w, y, color);          // Top
    renderer_draw_line(r, x + w, y, x + w, y + h, color);  // Right
    renderer_draw_line(r, x + w, y + h, x, y + h, color);  // Bottom
    renderer_draw_line(r, x, y + h, x, y, color);          // Left
}

void renderer_draw_line(renderer_t* r, float x0, float y0, float x1, float y1, float color[4]) {
    r->bind.views[0] = r->white_view;
    for (int i = 1; i < 12; i++) r->bind.views[i] = r->black_view;

    sg_apply_pipeline(r->pip_lines);
    sg_apply_bindings(&r->bind);

    mat4x4 proj, model, mvp;
    mat4x4_ortho(proj, 0, r->view_width, r->view_height, 0, -1.0f, 1.0f);
    mat4x4_identity(model);

    float dx = x1 - x0;
    float dy = y1 - y0;
    float dist = sqrtf(dx * dx + dy * dy);
    float angle = atan2f(dy, dx) * (180.0f / M_PI);

    mat4x4_translate_in_place(model, x0, y0, 0.0f);
    mat4x4_rotate_Z(model, model, angle * (M_PI / 180.0f));
    mat4x4_scale_aniso(model, model, dist, 1.0f, 1.0f);
    mat4x4_mul(mvp, proj, model);

    sg_range mvp_range = SG_RANGE(mvp);
    sg_apply_uniforms(0, &mvp_range);
    sg_range tint_range = {.ptr = color, .size = sizeof(float) * 4};
    sg_apply_uniforms(1, &tint_range);
    sg_draw(0, 6, 1);
    r->draw_calls++;

    r->bind.views[0] = (sg_view){SG_INVALID_ID};
}

void renderer_cleanup(renderer_t* r) {
    // RAII wrappers auto-destroy pipelines, images, views, buffers, and sampler.
    // Just reset IDs to trigger cleanup.
    r->pip_alpha = {};
    r->pip_add = {};
    r->pip_lines = {};
    r->vertex_buffer = {};
    r->index_buffer = {};
    r->smp = {};
    r->white_pixel = {};
    r->white_view = {};
    r->black_pixel = {};
    r->black_view = {};
    r->gray_pixel = {};
    r->gray_view = {};
}