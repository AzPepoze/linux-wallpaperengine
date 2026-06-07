#include "render.h"

#include <math.h>

#include "../../libs/sokol/sokol_glue.h"

void renderer_init(renderer_t* r, float w, float h) {
    r->view_width = w;
    r->view_height = h;

    // Standard quad: top-left at 0,0 with size 1x1
    vertex_t vertices[] = {
        {0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}};
    r->bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){.data = SG_RANGE(vertices)});

    uint16_t indices[] = {0, 1, 2, 0, 2, 3};
    r->bind.index_buffer =
        sg_make_buffer(&(sg_buffer_desc){.usage = {.index_buffer = true}, .data = SG_RANGE(indices)});

    r->smp = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_REPEAT,
        .wrap_v = SG_WRAP_REPEAT,
    });
    r->bind.samplers[0] = r->smp;

    sg_shader shd = sg_make_shader(&(sg_shader_desc){
        .vertex_func.source = "#version 330\n"
                              "uniform mat4 mvp;\n"
                              "layout(location=0) in vec2 position;\n"
                              "layout(location=1) in vec2 texcoord0;\n"
                              "out vec2 uv;\n"
                              "void main() {\n"
                              "  gl_Position = mvp * vec4(position, 0.0, 1.0);\n"
                              "  uv = texcoord0;\n"
                              "}\n",
        .fragment_func.source = "#version 330\n"
                                "precision mediump float;\n"
                                "uniform sampler2D tex;\n"
                                "uniform vec4 tint;\n"
                                "in vec2 uv;\n"
                                "out vec4 frag_color;\n"
                                "void main() {\n"
                                "  frag_color = texture(tex, uv) * tint;\n"
                                "}\n",
        .uniform_blocks[0] = {.stage = SG_SHADERSTAGE_VERTEX,
                              .size = sizeof(mat4x4),
                              .glsl_uniforms = {[0] = {.glsl_name = "mvp", .type = SG_UNIFORMTYPE_MAT4}}},
        .uniform_blocks[1] = {.stage = SG_SHADERSTAGE_FRAGMENT,
                              .size = sizeof(float) * 4,
                              .glsl_uniforms = {[0] = {.glsl_name = "tint", .type = SG_UNIFORMTYPE_FLOAT4}}},
        .views[0] = {.texture = {.stage = SG_SHADERSTAGE_FRAGMENT, .image_type = SG_IMAGETYPE_2D}},
        .samplers[0] = {.stage = SG_SHADERSTAGE_FRAGMENT, .sampler_type = SG_SAMPLERTYPE_FILTERING},
        .texture_sampler_pairs[0] = {
            .stage = SG_SHADERSTAGE_FRAGMENT, .glsl_name = "tex", .view_slot = 0, .sampler_slot = 0}});

    r->pip = sg_make_pipeline(&(sg_pipeline_desc){
        .shader = shd,
        .index_type = SG_INDEXTYPE_UINT16,
        .layout = {.attrs = {[0] = {.format = SG_VERTEXFORMAT_FLOAT2}, [1] = {.format = SG_VERTEXFORMAT_FLOAT2}}},
        .colors[0].blend = {.enabled = true,
                            .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
                            .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA}});
}

void renderer_update_viewport(renderer_t* r, float w, float h) {
    r->view_width = w;
    r->view_height = h;
}

void renderer_draw_sprite(renderer_t* r, sg_image img, float x, float y, float w, float h, float rotation,
                          float tint[4]) {
    sg_view view = sg_make_view(&(sg_view_desc){.texture = {.image = img}});
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

    sg_apply_uniforms(0, &SG_RANGE(mvp));

    sg_range tint_range = {.ptr = tint, .size = sizeof(float) * 4};
    sg_apply_uniforms(1, &tint_range);

    sg_draw(0, 6, 1);

    sg_destroy_view(view);
}

void renderer_cleanup(renderer_t* r) {
    sg_destroy_pipeline(r->pip);
    sg_destroy_buffer(r->bind.vertex_buffers[0]);
    sg_destroy_buffer(r->bind.index_buffer);
    sg_destroy_sampler(r->smp);
}
