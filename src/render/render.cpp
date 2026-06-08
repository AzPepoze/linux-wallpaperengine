#include "render.h"

#include <math.h>

#include "../../libs/sokol/sokol_glue.h"

void renderer_init(renderer_t* r, float w, float h) {
    r->view_width = w;
    r->view_height = h;

    // Standard quad: top-left at 0,0 with size 1x1
    vertex_t vertices[] = {
        {0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}};
    sg_buffer_desc v_desc = {};
    v_desc.data = SG_RANGE(vertices);
    r->bind.vertex_buffers[0] = sg_make_buffer(&v_desc);

    uint16_t indices[] = {0, 1, 2, 0, 2, 3};
    sg_buffer_desc i_desc = {};
    i_desc.usage.index_buffer = true;
    i_desc.data = SG_RANGE(indices);
    r->bind.index_buffer = sg_make_buffer(&i_desc);

    sg_sampler_desc s_desc = {};
    s_desc.min_filter = SG_FILTER_LINEAR;
    s_desc.mag_filter = SG_FILTER_LINEAR;
    s_desc.wrap_u = SG_WRAP_REPEAT;
    s_desc.wrap_v = SG_WRAP_REPEAT;
    r->smp = sg_make_sampler(&s_desc);
    r->bind.samplers[0] = r->smp;

    sg_shader_desc shd_desc = {};
    shd_desc.vertex_func.source =
        "#version 330\n"
        "uniform mat4 mvp;\n"
        "layout(location=0) in vec2 position;\n"
        "layout(location=1) in vec2 texcoord0;\n"
        "out vec2 uv;\n"
        "void main() {\n"
        "  gl_Position = mvp * vec4(position, 0.0, 1.0);\n"
        "  uv = texcoord0;\n"
        "}\n";
    shd_desc.fragment_func.source =
        "#version 330\n"
        "precision mediump float;\n"
        "uniform sampler2D tex;\n"
        "uniform vec4 tint;\n"
        "in vec2 uv;\n"
        "out vec4 frag_color;\n"
        "void main() {\n"
        "  frag_color = texture(tex, uv) * tint;\n"
        "}\n";
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

    sg_shader shd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    pip_desc.index_type = SG_INDEXTYPE_UINT16;
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    r->pip = sg_make_pipeline(&pip_desc);
}

void renderer_update_viewport(renderer_t* r, float w, float h) {
    r->view_width = w;
    r->view_height = h;
}

void renderer_draw_sprite(renderer_t* r, sg_image img, float x, float y, float w, float h, float rotation,
                          float tint[4]) {
    sg_view_desc v_desc = {};
    v_desc.texture.image = img;
    sg_view view = sg_make_view(&v_desc);
    r->bind.views[0] = view;

    sg_apply_pipeline(r->pip);
    sg_apply_bindings(&r->bind);

    mat4x4 proj, model, mvp;
    mat4x4_ortho(proj, 0, r->view_width, r->view_height, 0, -1.0f, 1.0f);

    mat4x4_identity(model);
    mat4x4_translate_in_place(model, x, y, 0.0f);
    mat4x4_rotate_Z(model, model, rotation * (M_PI / 180.0f));
    mat4x4_scale_aniso(model, model, w, h, 1.0f);

    mat4x4_mul(mvp, proj, model);

    sg_range mvp_range = SG_RANGE(mvp);
    sg_apply_uniforms(0, &mvp_range);

    sg_range tint_range = {.ptr = tint, .size = sizeof(float) * 4};
    sg_apply_uniforms(1, &tint_range);

    sg_draw(0, 6, 1);

    sg_destroy_view(view);
}

void renderer_draw_rect(renderer_t* r, float x, float y, float w, float h, float color[4]) {
    // Re-use standard sprite shader but without a texture (or with a white pixel)
    // For simplicity, we just draw a colored quad.
    // In a more advanced renderer, we might use a dedicated shader.

    // We use SG_INVALID_ID to signify "no texture" or just use a fallback if needed
    // But since our current shader always expects a texture, we'll just not bind a view
    // and see if it works as a flat color (depends on GL driver behavior)
    // Better: We could create a 1x1 white texture during init.

    sg_apply_pipeline(r->pip);
    sg_apply_bindings(&r->bind);

    mat4x4 proj, model, mvp;
    mat4x4_ortho(proj, 0, r->view_width, r->view_height, 0, -1.0f, 1.0f);

    mat4x4_identity(model);
    mat4x4_translate_in_place(model, x, y, 0.0f);
    mat4x4_scale_aniso(model, model, w, h, 1.0f);

    mat4x4_mul(mvp, proj, model);

    sg_range mvp_range = SG_RANGE(mvp);
    sg_apply_uniforms(0, &mvp_range);

    sg_range tint_range = {.ptr = color, .size = sizeof(float) * 4};
    sg_apply_uniforms(1, &tint_range);

    sg_draw(0, 6, 1);
}

void renderer_cleanup(renderer_t* r) {
    sg_destroy_pipeline(r->pip);
    sg_destroy_buffer(r->bind.vertex_buffers[0]);
    sg_destroy_buffer(r->bind.index_buffer);
    sg_destroy_sampler(r->smp);
}
