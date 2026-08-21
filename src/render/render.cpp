#include "render.h"

#include <math.h>

#include <string>

#include "../core/context.h"
#include "../core/engine_context.h"
#include "../core/logger.h"
#include "shader/shader_backend.h"
#include "shader/shader_compiler.h"
#include "sokol_glue.h"

void renderer_init(renderer_t* r, float w, float h) {
    r->view_width = w;
    r->view_height = h;

    vertex_t vertices[] = {
        {0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}};
    sg_buffer_desc v_desc = {};
    v_desc.data = SG_RANGE(vertices);
    r->vertex_buffer = sg_make_buffer(&v_desc);
    r->bind.vertex_buffers[0] = r->vertex_buffer;

    vertex_t fullscreen_vertices[] = {
        {-1.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 0.0f}, {1.0f, -1.0f, 1.0f, 1.0f}, {-1.0f, -1.0f, 0.0f, 1.0f}};
    sg_buffer_desc fsv_desc = {};
    fsv_desc.data = SG_RANGE(fullscreen_vertices);
    r->fullscreen_vertex_buffer = sg_make_buffer(&fsv_desc);

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
    r->smp_repeat = sg_make_sampler(&s_desc);
    s_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    s_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    r->smp_clamp = sg_make_sampler(&s_desc);
    for (int i = 0; i < SG_MAX_SAMPLER_BINDSLOTS; i++) {
        r->bind.samplers[i] = r->smp_repeat;
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

    pixel = 0x808080FF;  // Retained as a general-purpose neutral gray fallback/debug texture.
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
    // Scene composition must keep an opaque accumulated target opaque.  With
    // the backend defaults, a translucent solid/opacity layer replaced the
    // target alpha with its mask alpha; presenting that target then multiplied
    // the already-composited scene by the mask a second time.
    pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    r->pip_alpha = sg_make_pipeline(&pip_desc);

    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE;
    r->pip_add = sg_make_pipeline(&pip_desc);

    // Line Pipeline
    pip_desc.primitive_type = SG_PRIMITIVETYPE_LINES;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    r->pip_lines = sg_make_pipeline(&pip_desc);

    const std::string composite_vertex =
        "#version 330\n"
        "uniform mat4 g_ModelViewProjectionMatrix; layout(location=0) in vec2 a_Position; "
        "layout(location=1) in vec2 a_TexCoord; out vec2 v_TexCoord; out vec2 v_SceneUV; "
        "void main(){ gl_Position=g_ModelViewProjectionMatrix*vec4(a_Position,0,1); "
        "v_TexCoord=a_TexCoord; v_SceneUV=vec2(gl_Position.x*0.5+0.5,0.5-gl_Position.y*0.5); }\n";
    for (int mode = 1; mode <= 30; ++mode) {
        std::string fragment =
            "#version 330\nprecision mediump float; uniform sampler2D g_Texture0; uniform sampler2D g_Texture1; "
            "uniform vec4 tint; in vec2 v_TexCoord; in vec2 v_SceneUV; out vec4 frag_color; "
            "vec3 rgb2hsl(vec3 c){float lo=min(min(c.r,c.g),c.b),hi=max(max(c.r,c.g),c.b),d=hi-lo,l=(hi+lo)*.5;"
            "if(d==0.)return vec3(0.,0.,l);float s=l<.5?d/(hi+lo):d/(2.-hi-lo),h;"
            "if(hi==c.r)h=(c.g-c.b)/d+(c.g<c.b?6.:0.);else if(hi==c.g)h=(c.b-c.r)/d+2.;"
            "else h=(c.r-c.g)/d+4.;return vec3(h/6.,s,l);}"
            "float hue2rgb(float p,float q,float h){h=fract(h);if(6.*h<1.)return p+(q-p)*6.*h;"
            "if(2.*h<1.)return q;if(3.*h<2.)return p+(q-p)*(2./3.-h)*6.;return p;}"
            "vec3 hsl2rgb(vec3 hsl){if(hsl.y==0.)return vec3(hsl.z);float q=hsl.z<.5?hsl.z*(1.+hsl.y):"
            "hsl.z+hsl.y-hsl.z*hsl.y,p=2.*hsl.z-q;return vec3(hue2rgb(p,q,hsl.x+1./3.),hue2rgb(p,q,hsl.x),"
            "hue2rgb(p,q,hsl.x-1./3.));}"
            "vec3 blend(vec3 b,vec3 s,float o){";
        // Wallpaper Engine's image blend modes are shader operations, not framebuffer blend states.
        if (mode == 2)
            fragment += "return mix(b,b*s,o);";  // Multiply
        else if (mode == 11)
            fragment += "return mix(b,mix(2.0*b*s,1.0-2.0*(1.0-b)*(1.0-s),step(0.5,b)),o);";  // Overlay
        else if (mode == 1)
            fragment += "return mix(b,min(b,s),o);";
        else if (mode == 3)
            fragment += "return mix(b,max(1.0-(1.0-b)/max(s,vec3(.001)),0.0),o);";
        else if (mode == 4)
            fragment += "return mix(b,max(b+s-1.0,0.0),o);";
        else if (mode == 5)
            fragment += "return min(b,s);";
        else if (mode == 6)
            fragment += "return mix(b,max(b,s),o);";
        else if (mode == 7)
            fragment += "return mix(b,1.0-(1.0-b)*(1.0-s),o);";
        else if (mode == 8)
            fragment += "return mix(b,min(b/max(1.0-s,vec3(.001)),1.0),o);";
        else if (mode == 9)
            fragment += "return mix(b,min(b+s,1.0),o);";
        else if (mode == 10)
            fragment += "return max(b,s);";
        else if (mode == 12)
            fragment += "return mix(b,mix(2.0*b*s+b*b*(1.0-2.0*s),sqrt(b)*(2.0*s-1.0)+2.0*b*(1.0-s),step(.5,s)),o);";
        else if (mode == 13)
            fragment += "return mix(b,mix(2.0*b*s,1.0-2.0*(1.0-b)*(1.0-s),step(.5,s)),o);";
        else if (mode == 14)
            fragment +=
                "return "
                "mix(b,mix(max(1.0-(1.0-b)/max(2.0*s,vec3(.001)),0.0),min(b/"
                "max(2.0*(s-.5),vec3(.001)),1.0),step(.5,s)),o);";
        else if (mode == 15)
            fragment += "return mix(b,mix(max(b+2.0*s-1.0,0.0),min(b+2.0*(s-.5),1.0),step(.5,s)),o);";
        else if (mode == 16)
            fragment += "return mix(b,mix(min(b,2.0*s),max(b,2.0*(s-.5)),step(.5,s)),o);";
        else if (mode == 17)
            fragment +=
                "return "
                "mix(b,step(.5,mix(max(1.0-(1.0-b)/max(2.0*s,vec3(.001)),0.0),min(b/"
                "max(2.0*(s-.5),vec3(.001)),1.0),step(.5,s))),o);";
        else if (mode == 18)
            fragment += "return mix(b,abs(b-s),o);";
        else if (mode == 19)
            fragment += "return mix(b,b+s-2.0*b*s,o);";
        else if (mode == 20)
            fragment += "return mix(b,max(b+s-1.0,0.0),o);";
        else if (mode == 21)
            fragment += "return mix(b,min(b*b/max(1.0-s,vec3(.001)),1.0),o);";
        else if (mode == 22)
            fragment += "return mix(b,min(s*s/max(1.0-b,vec3(.001)),1.0),o);";
        else if (mode == 23)
            fragment += "return mix(b,min(b,s)-max(b,s)+1.0,o);";
        else if (mode == 24)
            fragment += "return mix(b,(b+s)*.5,o);";
        else if (mode == 25)
            fragment += "return mix(b,1.0-abs(1.0-b-s),o);";
        else if (mode == 26)
            fragment += "vec3 bh=rgb2hsl(b),sh=rgb2hsl(s);return mix(b,hsl2rgb(vec3(sh.x,bh.y,bh.z)),o);";
        else if (mode == 27)
            fragment += "vec3 bh=rgb2hsl(b),sh=rgb2hsl(s);return mix(b,hsl2rgb(vec3(bh.x,sh.y,bh.z)),o);";
        else if (mode == 28)
            fragment += "vec3 bh=rgb2hsl(b),sh=rgb2hsl(s);return mix(b,hsl2rgb(vec3(sh.x,sh.y,bh.z)),o);";
        else if (mode == 29)
            fragment += "vec3 bh=rgb2hsl(b),sh=rgb2hsl(s);return mix(b,hsl2rgb(vec3(bh.x,bh.y,sh.z)),o);";
        else if (mode == 30)
            fragment += "return mix(b,vec3(max(max(b.r,b.g),b.b))*s,o);";
        else
            fragment += "return mix(b,s,o);";
        fragment +=
            "} void main(){vec4 source=texture(g_Texture0,v_TexCoord)*tint; "
            "vec4 background=texture(g_Texture1,v_SceneUV); "
            "frag_color=vec4(blend(background.rgb,source.rgb,source.a),1.0);}";
        CompiledShader shader =
            ShaderCompiler::compile("image-composite-" + std::to_string(mode), composite_vertex, fragment, {}, 1);
        r->pip_image_composite[mode] = std::move(shader.pipeline);
    }
}

void renderer_draw_sprite(EngineContext& ctx, renderer_t* r, sg_image img, sg_view main_view, float x, float y, float w,
                          float h, float rotation, float tint[4], bool additive, const render_effect_pass_t* pass) {
    mat4x4 proj, model, mvp;
    mat4x4_ortho(proj, 0, r->view_width, r->view_height, 0, -1.0f, 1.0f);
    mat4x4_identity(model);
    mat4x4_translate_in_place(model, x, y, 0.0f);
    mat4x4_rotate_Z(model, model, rotation * (M_PI / 180.0f));
    mat4x4_scale_aniso(model, model, w, h, 1.0f);
    mat4x4_mul(mvp, proj, model);

    for (int i = 0; i < SG_MAX_SAMPLER_BINDSLOTS; ++i) r->bind.samplers[i] = r->smp_repeat;

    if (pass && pass->enabled && pass->pipeline.id != SG_INVALID_ID) {
        sg_apply_pipeline(pass->pipeline);
        r->bind.vertex_buffers[0] = pass->is_fullscreen_quad ? r->fullscreen_vertex_buffer : r->vertex_buffer;
        r->bind.samplers[0] = pass->repeat_effect_input ? r->smp_repeat : r->smp_clamp;

        // Built-in Uniforms Setup
        builtin_uniforms_t builtin = {};
        memcpy(builtin.mvp, mvp, sizeof(mat4x4));
        mat4x4_invert(builtin.mvp_inverse, mvp);
        builtin.parallax_pos[0] = ctx.parallax_smooth_x * 0.5f + 0.5f;
        builtin.parallax_pos[1] = ctx.parallax_smooth_y * 0.5f + 0.5f;
        builtin.time = ctx.time;
        builtin.screen_res[0] = r->view_width;
        builtin.screen_res[1] = r->view_height;
        builtin.texel_size[0] = r->view_width > 0.0f ? 1.0f / r->view_width : 0.0f;
        builtin.texel_size[1] = r->view_height > 0.0f ? 1.0f / r->view_height : 0.0f;
        builtin.pointer_position[0] = 0.5f;
        builtin.pointer_position[1] = 0.5f;
        if (ctx.mouse_position_valid && r->view_width > 0.0f && r->view_height > 0.0f) {
            builtin.pointer_position[0] = std::max(0.0f, std::min(1.0f, ctx.mouse_x / r->view_width));
            builtin.pointer_position[1] = std::max(0.0f, std::min(1.0f, ctx.mouse_y / r->view_height));
        }
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

        const bool is_depth_parallax = pass->shader_name && strstr(pass->shader_name, "depthparallax") != nullptr;
        const bool is_waterwaves = pass->shader_name && strstr(pass->shader_name, "waterwaves") != nullptr;

        // Slot 1+ (Extra Textures from pass->extra_views)
        for (int i = 0; i < 11; i++) {
            int slot = i + 1;  // Shift by 1 because Slot 0 is the main view

            if (pass->override_views && i < (int)pass->num_override_views &&
                pass->override_views[i].id != SG_INVALID_ID) {
                r->bind.views[slot] = pass->override_views[i];
            } else if (pass->extra_views && i < (int)pass->num_extra_views &&
                       pass->extra_views[i].id != SG_INVALID_ID) {
                r->bind.views[slot] = pass->extra_views[i];
            } else if (i == 0) {
                // WPE metadata declares util/black for missing depthparallax depth and a full mask for waterwaves.
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
    if (pass && pass->enabled && pass->pipeline.id != SG_INVALID_ID) {
        if (pass->apply_custom_uniforms) {
            pass->apply_custom_uniforms(pass->user_data);
        }
    }

    sg_draw(0, 6, 1);
    r->draw_calls++;

    // Clean up bindings for next call
    for (int i = 0; i < 12; i++) {
        r->bind.views[i] = (sg_view){SG_INVALID_ID};
    }
}

void renderer_draw_image_composite(EngineContext& ctx, renderer_t* r, sg_image image, sg_view image_view,
                                   sg_view scene_view, float x, float y, float width, float height, float rotation,
                                   float tint[4], int blend_mode) {
    if (blend_mode == 31) {
        renderer_draw_sprite(ctx, r, image, image_view, x, y, width, height, rotation, tint, true, nullptr);
        return;
    }
    if (blend_mode < 1 || blend_mode > 30 || r->pip_image_composite[blend_mode].id == SG_INVALID_ID) {
        renderer_draw_sprite(ctx, r, image, image_view, x, y, width, height, rotation, tint, false, nullptr);
        return;
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
