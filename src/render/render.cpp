#include "render.h"

#include <math.h>

#include <string>

#include "../core/context.h"
#include "../core/engine_context.h"
#include "../core/logger.h"
#include "shader/shader_backend.h"
#include "shader/shader_compiler.h"
#include "shader/shader_processor.h"
#include "sokol_glue.h"

namespace {
constexpr int kFirstWallpaperBlendMode = 1;
constexpr int kLastWallpaperBlendMode = 30;

GfxPipeline compileWallpaperBlendPipeline(EngineContext& ctx, int blend_mode) {
    const std::string vertex_source =
        "#version 330\n"
        "uniform mat4 g_ModelViewProjectionMatrix;\n"
        "layout(location=0) in vec2 a_Position;\n"
        "layout(location=1) in vec2 a_TexCoord;\n"
        "out vec2 v_TexCoord;\n"
        "out vec2 v_SceneUV;\n"
        "void main() {\n"
        "    gl_Position = g_ModelViewProjectionMatrix * vec4(a_Position, 0.0, 1.0);\n"
        "    v_TexCoord = a_TexCoord;\n"
        "    v_SceneUV = vec2(gl_Position.x * 0.5 + 0.5, 0.5 - gl_Position.y * 0.5);\n"
        "}\n";

    const std::string fragment_source =
        "#version 330\n"
        "precision mediump float;\n"
        "#include \"common_blending.h\"\n"
        "uniform sampler2D g_Texture0;\n"
        "uniform sampler2D g_Texture1;\n"
        "uniform vec4 tint;\n"
        "in vec2 v_TexCoord;\n"
        "in vec2 v_SceneUV;\n"
        "out vec4 frag_color;\n"
        "void main() {\n"
        "    vec4 source = texture(g_Texture0, v_TexCoord) * tint;\n"
        "    vec4 background = texture(g_Texture1, v_SceneUV);\n"
        "    frag_color = vec4(ApplyBlending(BLENDMODE, background.rgb, source.rgb, source.a), 1.0);\n"
        "}\n";

    std::string processed_vertex = ShaderSourceProcessor::processShaderSource(
        vertex_source, "shaders/linux-wallpaperengine/image_composite.vert", ctx.asset_mgr, true);
    std::string processed_fragment = ShaderSourceProcessor::processShaderSource(
        fragment_source, "shaders/linux-wallpaperengine/image_composite.frag", ctx.asset_mgr, false);

    // Wallpaper Engine shader data is a runtime requirement. Do not substitute an
    // approximate or normal-alpha implementation when its authoritative header is unavailable.
    if (processed_fragment.find("#include \"common_blending.h\"") != std::string::npos) {
        LOG_TAG_E("RENDER", "Required Wallpaper Engine common_blending.h was not found for blend mode %d", blend_mode);
        return {};
    }

    const std::string prefix = ShaderSourceProcessor::buildShaderPrefix();
    const std::string blend_define = "#define BLENDMODE " + std::to_string(blend_mode) + "\n";
    CompiledShader shader =
        ShaderCompiler::compile("image-composite-" + std::to_string(blend_mode), prefix + processed_vertex,
                                prefix + blend_define + processed_fragment, {}, 1);
    if (shader.pipeline.id == SG_INVALID_ID) {
        LOG_TAG_E("RENDER", "Required Wallpaper Engine blend mode %d failed to compile", blend_mode);
        return {};
    }
    return std::move(shader.pipeline);
}
}  // namespace

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

    for (int mode = 0; mode <= kLastWallpaperBlendMode; ++mode) {
        r->pip_image_composite[mode] = {};
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

void renderer_precompile_blend_pipelines(EngineContext& ctx, renderer_t* r) {
    for (int mode = kFirstWallpaperBlendMode; mode <= kLastWallpaperBlendMode; ++mode) {
        if (r->pip_image_composite[mode].id == SG_INVALID_ID) {
            r->pip_image_composite[mode] = compileWallpaperBlendPipeline(ctx, mode);
        }
    }
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

    sg_view background[] = {scene_view};
    render_effect_pass_t composite = {};
    composite.enabled = true;
    composite.pipeline = r->pip_image_composite[blend_mode];
    composite.shader_name = "image-composite";
    composite.override_views = background;
    composite.num_override_views = 1;

    renderer_draw_sprite(ctx, r, image, image_view, x, y, width, height, rotation, tint, false, &composite);
}
