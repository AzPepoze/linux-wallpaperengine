#include "scene_2d.h"

#include "shared/graphics/render.h"
#include "sokol_app.h"
#include "wallpaper/2d/layers/image_layer.h"
#include "wallpaper/2d/layers/layer.h"
#include "wallpaper/2d/layers/particle_layer.h"
#include "wallpaper/2d/tree/scene_tree.h"

#if DEBUG_BUILD
#include "shared/graphics/diagnostics/render_diagnostics.h"
#endif

void Scene2DRuntime::init() {
    renderer_init(&ctx.renderer, (float)sapp_width(), (float)sapp_height());
}

void Scene2DRuntime::initBloomPipelines() {
    const std::string vs_source =
        "#version 330\n"
        "uniform mat4 g_ModelViewProjectionMatrix;\n"
        "layout(location=0) in vec2 a_Position;\n"
        "layout(location=1) in vec2 a_TexCoord;\n"
        "out vec2 v_TexCoord;\n"
        "void main() {\n"
        "    gl_Position = g_ModelViewProjectionMatrix * vec4(a_Position, 0.0, 1.0);\n"
        "    v_TexCoord = a_TexCoord;\n"
        "}\n";

    const std::string fs_extract =
        "#version 330\n"
        "precision mediump float;\n"
        "uniform sampler2D g_Texture0;\n"
        // renderer_draw_sprite supplies its per-pass values through `tint`.
        // This mirrors the authored bloom constants without relying on an
        // unbound g_RenderVar0 uniform.\n"
        "uniform vec4 tint;\n"
        "in vec2 v_TexCoord;\n"
        "out vec4 frag_color;\n"
        "void main() {\n"
        "    vec4 col = texture(g_Texture0, v_TexCoord);\n"
        "    float brightness = max(col.r, max(col.g, col.b));\n"
        "    float threshold = tint.x;\n"
        "    float strength = tint.y;\n"
        "    float feather = max(0.01, tint.w);\n"
        "    float knee = threshold * feather;\n"
        "    float soft = brightness - threshold + knee;\n"
        "    soft = clamp(soft, 0.0, 2.0 * knee);\n"
        "    soft = soft * soft / (4.0 * max(0.0001, knee) + 0.00001);\n"
        "    float contribution = max(soft, brightness - threshold);\n"
        "    contribution /= max(brightness, 0.00001);\n"
        "    frag_color = vec4(col.rgb * contribution * strength, 1.0);\n"
        "}\n";

    const std::string fs_blur =
        "#version 330\n"
        "precision mediump float;\n"
        "uniform sampler2D g_Texture0;\n"
        "uniform vec2 g_TexelSize;\n"
        "uniform vec4 tint;\n"
        "in vec2 v_TexCoord;\n"
        "out vec4 frag_color;\n"
        "void main() {\n"
        "    float weights[5];\n"
        "    weights[0] = 0.227027;\n"
        "    weights[1] = 0.1945946;\n"
        "    weights[2] = 0.1216216;\n"
        "    weights[3] = 0.054054;\n"
        "    weights[4] = 0.016216;\n"
        "    vec3 result = texture(g_Texture0, v_TexCoord).rgb * weights[0];\n"
        "    for (int i = 1; i < 5; ++i) {\n"
        "        result += texture(g_Texture0, v_TexCoord + g_TexelSize * tint.x * float(i)).rgb * weights[i];\n"
        "        result += texture(g_Texture0, v_TexCoord - g_TexelSize * tint.x * float(i)).rgb * weights[i];\n"
        "    }\n"
        "    frag_color = vec4(result, 1.0);\n"
        "}\n";

    pip_bloom_extract = ShaderCompiler::compile("bloom_extract", vs_source, fs_extract, {}, 1).pipeline;
    pip_bloom_blur_h = ShaderCompiler::compile("bloom_blur_h", vs_source, fs_blur, {}, 1).pipeline;
    pip_bloom_blur_v = ShaderCompiler::compile("bloom_blur_v", vs_source, fs_blur, {}, 1).pipeline;
}

void Scene2DRuntime::update(float dt) {
    if (ctx.test_mode && ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
        ctx.layers[ctx.selected_object]->update(dt, ctx);
        return;
    }
    for (auto layer : ctx.layers) layer->update(dt, ctx);
}

bool Scene2DRuntime::requiresOffscreenComposition() const {
    const float bloom_strength = ctx.general.hdr ? ctx.general.bloom.hdr_strength : ctx.general.bloom.strength;
    if (ctx.general.bloom.enabled && bloom_strength > 0.0f) return true;

    if (ctx.test_mode && ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
        const auto* particle = dynamic_cast<const ParticleLayer*>(ctx.layers[ctx.selected_object]);
        return particle && particle->requiresSceneColor();
    }

    bool any_solo = false;
    for (const auto* layer : ctx.layers) {
        if (layer->solo) {
            any_solo = true;
            break;
        }
    }

    for (const auto* layer : ctx.layers) {
        if ((any_solo && !layer->solo) || (!any_solo && !layer->visible)) continue;
        const auto* particle = dynamic_cast<const ParticleLayer*>(layer);
        if (particle && particle->requiresSceneColor()) return true;
        const auto* image = dynamic_cast<const ImageLayer*>(layer);
        if (image && image->requiresSceneColor()) return true;
    }
    return false;
}

sg_pixel_format Scene2DRuntime::compositionPixelFormat() const {
    if (!ctx.general.hdr) return SG_PIXELFORMAT_RGBA8;
    // A floating-point attachment keeps HDR bloom energy until presentation.
    // Drivers without it reject creation below, where RGBA8 is retried.
    return SG_PIXELFORMAT_RGBA16F;
}

bool Scene2DRuntime::ensureSceneTargets(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    const sg_pixel_format requested_format = compositionPixelFormat();
    if (scene_targets[0].image.id != SG_INVALID_ID && scene_targets[0].width == width &&
        scene_targets[0].height == height && scene_targets[1].image.id != SG_INVALID_ID &&
        scene_targets[1].width == width && scene_targets[1].height == height &&
        scene_targets[0].pixel_format == requested_format && scene_targets[1].pixel_format == requested_format) {
        return true;
    }

    scene_targets[0] = SceneTarget{};
    scene_targets[1] = SceneTarget{};

    for (SceneTarget& target : scene_targets) {
        sg_image_desc image_desc = {};
        image_desc.usage.color_attachment = true;
        image_desc.width = width;
        image_desc.height = height;
        image_desc.pixel_format = requested_format;
        target.image = sg_make_image(&image_desc);
        if (target.image.id == SG_INVALID_ID && requested_format != SG_PIXELFORMAT_RGBA8) {
            image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
            target.image = sg_make_image(&image_desc);
        }
        if (target.image.id == SG_INVALID_ID) return false;

        sg_view_desc texture_desc = {};
        texture_desc.texture.image = target.image;
        target.texture_view = sg_make_view(&texture_desc);

        sg_view_desc attachment_desc = {};
        attachment_desc.color_attachment.image = target.image;
        target.attachment_view = sg_make_view(&attachment_desc);
        target.width = width;
        target.height = height;
        target.pixel_format = image_desc.pixel_format;

        if (target.texture_view.id == SG_INVALID_ID || target.attachment_view.id == SG_INVALID_ID) return false;
    }
    return true;
}

bool Scene2DRuntime::ensureBloomTargets(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    const sg_pixel_format requested_format = compositionPixelFormat();
    if (bloom_targets[0].image.id != SG_INVALID_ID && bloom_targets[0].width == width &&
        bloom_targets[0].height == height && bloom_targets[1].image.id != SG_INVALID_ID &&
        bloom_targets[1].width == width && bloom_targets[1].height == height &&
        bloom_targets[0].pixel_format == requested_format && bloom_targets[1].pixel_format == requested_format) {
        return true;
    }

    bloom_targets[0] = SceneTarget{};
    bloom_targets[1] = SceneTarget{};

    for (SceneTarget& target : bloom_targets) {
        sg_image_desc image_desc = {};
        image_desc.usage.color_attachment = true;
        image_desc.width = width;
        image_desc.height = height;
        image_desc.pixel_format = requested_format;
        target.image = sg_make_image(&image_desc);
        if (target.image.id == SG_INVALID_ID && requested_format != SG_PIXELFORMAT_RGBA8) {
            image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
            target.image = sg_make_image(&image_desc);
        }
        if (target.image.id == SG_INVALID_ID) return false;

        sg_view_desc texture_desc = {};
        texture_desc.texture.image = target.image;
        target.texture_view = sg_make_view(&texture_desc);

        sg_view_desc attachment_desc = {};
        attachment_desc.color_attachment.image = target.image;
        target.attachment_view = sg_make_view(&attachment_desc);
        target.width = width;
        target.height = height;
        target.pixel_format = image_desc.pixel_format;

        if (target.texture_view.id == SG_INVALID_ID || target.attachment_view.id == SG_INVALID_ID) return false;
    }
    return true;
}

void Scene2DRuntime::renderBloom(int current_target_index, int width, int height) {
    const bool hdr = ctx.general.hdr;
    const float strength = hdr ? ctx.general.bloom.hdr_strength : ctx.general.bloom.strength;
    if (!ctx.general.bloom.enabled || strength <= 0.0f) return;
    if (pip_bloom_extract.id == SG_INVALID_ID) {
        initBloomPipelines();
    }
    if (pip_bloom_extract.id == SG_INVALID_ID || pip_bloom_blur_h.id == SG_INVALID_ID ||
        pip_bloom_blur_v.id == SG_INVALID_ID) {
        return;
    }

    // Wallpaper Engine's LDR path starts at quarter resolution; HDR uses the
    // same first mip with a threshold/knee extraction before scattering it.
    const int bloom_w = std::max(1, width / 4);
    const int bloom_h = std::max(1, height / 4);
    if (!ensureBloomTargets(bloom_w, bloom_h)) return;

    const float threshold = hdr ? ctx.general.bloom.hdr_threshold : ctx.general.bloom.threshold;
    const float scatter = hdr ? std::max(0.0f, ctx.general.bloom.hdr_scatter) : 1.0f;
    const float feather = hdr ? std::max(0.0f, ctx.general.bloom.hdr_feather) : 0.0f;
    const int blur_iterations = hdr ? std::max(1, std::min(16, (int)std::lround(ctx.general.bloom.hdr_iterations))) : 1;

    // Pass 1: Extract bright pixels to bloom_targets[0]
    {
        sg_pass extract_pass = {};
        extract_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        extract_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
        extract_pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};
        extract_pass.attachments.colors[0] = bloom_targets[0].attachment_view;
        sg_begin_pass(&extract_pass);
        renderer_update_viewport(&ctx.renderer, (float)bloom_w, (float)bloom_h);

        render_effect_pass_t pass_desc = {};
        pass_desc.enabled = true;
        pass_desc.pipeline = pip_bloom_extract;
        pass_desc.shader_name = "bloom_extract";
        float tint[4] = {threshold, strength, scatter, feather};
        renderer_draw_sprite(ctx, &ctx.renderer, scene_targets[current_target_index].image,
                             scene_targets[current_target_index].texture_view, 0.0f, 0.0f, (float)bloom_w,
                             (float)bloom_h, 0.0f, tint, false, &pass_desc);
        sg_end_pass();
    }

    // Passes 2/3: secondary mip approximation and separable scatter blur.
    // Repeating the ping-pong blur gives HDR iterations a real authored effect.
    for (int iteration = 0; iteration < blur_iterations; ++iteration) {
        sg_pass blur_h_pass = {};
        blur_h_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        blur_h_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
        blur_h_pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};
        blur_h_pass.attachments.colors[0] = bloom_targets[1].attachment_view;
        sg_begin_pass(&blur_h_pass);
        renderer_update_viewport(&ctx.renderer, (float)bloom_w, (float)bloom_h);

        render_effect_pass_t pass_desc = {};
        pass_desc.enabled = true;
        pass_desc.pipeline = pip_bloom_blur_h;
        pass_desc.shader_name = "bloom_blur_h";
        float tint[4] = {scatter, 1.0f, 1.0f, 1.0f};
        renderer_draw_sprite(ctx, &ctx.renderer, bloom_targets[0].image, bloom_targets[0].texture_view, 0.0f, 0.0f,
                             (float)bloom_w, (float)bloom_h, 0.0f, tint, false, &pass_desc);
        sg_end_pass();

        sg_pass blur_v_pass = {};
        blur_v_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        blur_v_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
        blur_v_pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};
        blur_v_pass.attachments.colors[0] = bloom_targets[0].attachment_view;
        sg_begin_pass(&blur_v_pass);
        renderer_update_viewport(&ctx.renderer, (float)bloom_w, (float)bloom_h);

        render_effect_pass_t vertical_pass_desc = {};
        vertical_pass_desc.enabled = true;
        vertical_pass_desc.pipeline = pip_bloom_blur_v;
        vertical_pass_desc.shader_name = "bloom_blur_v";
        float vertical_tint[4] = {scatter, 1.0f, 1.0f, 1.0f};
        renderer_draw_sprite(ctx, &ctx.renderer, bloom_targets[1].image, bloom_targets[1].texture_view, 0.0f, 0.0f,
                             (float)bloom_w, (float)bloom_h, 0.0f, vertical_tint, false, &vertical_pass_desc);
        sg_end_pass();
    }

    // Pass 4: Combine Additive over scene_targets[current_target_index]
    {
        sg_pass combine_pass = {};
        combine_pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
        combine_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
        combine_pass.attachments.colors[0] = scene_targets[current_target_index].attachment_view;
        sg_begin_pass(&combine_pass);
        renderer_update_viewport(&ctx.renderer, (float)width, (float)height);

        float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        renderer_draw_sprite(ctx, &ctx.renderer, bloom_targets[0].image, bloom_targets[0].texture_view, 0.0f, 0.0f,
                             (float)width, (float)height, 0.0f, white, true, nullptr);
        sg_end_pass();
    }
}

void Scene2DRuntime::drawDirect() {
    const bool has_output_viewport = output_width > 0 && output_height > 0;
    if (has_output_viewport) {
        sg_apply_viewport(output_x, output_y, output_width, output_height, true);
        sg_apply_scissor_rect(output_x, output_y, output_width, output_height, true);
    }

    if (ctx.test_mode && ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
        ctx.layers[ctx.selected_object]->draw(ctx);
    } else {
        bool any_solo = false;
        for (auto layer : ctx.layers) {
            if (layer->solo) {
                any_solo = true;
                break;
            }
        }

        for (auto layer : ctx.layers) {
            if (any_solo) {
                if (layer->solo) layer->draw(ctx);
            } else if (layer->visible) {
                layer->draw(ctx);
            }
        }
    }

    if (has_output_viewport) {
        sg_apply_viewport(0, 0, sapp_width(), sapp_height(), true);
        sg_apply_scissor_rect(0, 0, sapp_width(), sapp_height(), true);
    }
}

void Scene2DRuntime::drawOffscreen() {
    const int width = output_width > 0 ? output_width : sapp_width();
    const int height = output_height > 0 ? output_height : sapp_height();
    if (!ensureSceneTargets(width, height)) {
        scene_output_index = -1;
        return;
    }

    renderer_update_viewport(&ctx.renderer, (float)width, (float)height);
    sg_apply_viewport(0, 0, width, height, true);
    sg_apply_scissor_rect(0, 0, width, height, true);

    int current = 0;
    sg_pass clear_pass = {};
    clear_pass.action = ctx.pass_action;
    clear_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    clear_pass.attachments.colors[0] = scene_targets[current].attachment_view;
    sg_begin_pass(&clear_pass);
    sg_end_pass();

    int layer_index = 0;
    auto capture_layer_result = [&](Layer* layer, bool raw_layer) {
#if DEBUG_BUILD
        RenderDiagnostics& diagnostics = RenderDiagnostics::instance();
        if (!diagnostics.is_capturing_frame) return;

        sg_image_desc image_desc = {};
        image_desc.usage.color_attachment = true;
        image_desc.width = width;
        image_desc.height = height;
        image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        sg_image snapshot = sg_make_image(&image_desc);
        if (snapshot.id == SG_INVALID_ID) return;
        sg_view_desc source_view_desc = {};
        source_view_desc.texture.image = snapshot;
        sg_view snapshot_texture = sg_make_view(&source_view_desc);
        sg_view_desc attachment_view_desc = {};
        attachment_view_desc.color_attachment.image = snapshot;
        sg_view snapshot_attachment = sg_make_view(&attachment_view_desc);
        if (snapshot_texture.id == SG_INVALID_ID || snapshot_attachment.id == SG_INVALID_ID) {
            if (snapshot_texture.id != SG_INVALID_ID) sg_destroy_view(snapshot_texture);
            if (snapshot_attachment.id != SG_INVALID_ID) sg_destroy_view(snapshot_attachment);
            sg_destroy_image(snapshot);
            return;
        }

        sg_pass snapshot_pass = {};
        snapshot_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        snapshot_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
        snapshot_pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
        snapshot_pass.attachments.colors[0] = snapshot_attachment;
        sg_begin_pass(&snapshot_pass);
        if (raw_layer) {
            // Capture the layer's rendered output before it is blended with
            // the accumulated scene. This deliberately has no scene copy.
            layer->draw(ctx);
        } else {
            float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            renderer_draw_sprite(ctx, &ctx.renderer, scene_targets[current].image, scene_targets[current].texture_view,
                                 0.0f, 0.0f, (float)width, (float)height, 0.0f, white, false, nullptr);
        }
        sg_end_pass();

        char stage_name[320];
        if (const auto* image = dynamic_cast<const ImageLayer*>(layer)) {
            snprintf(stage_name, sizeof(stage_name), "%s-%02d-id-%u-%s-alpha-%.3f-blend-%d",
                     raw_layer ? "raw" : "after", layer_index, image->scene_object_id, layer->name.c_str(),
                     image->tint[3], image->color_blend_mode);
        } else {
            snprintf(stage_name, sizeof(stage_name), "%s-%02d-id-%u-%s", raw_layer ? "raw" : "after", layer_index,
                     layer->scene_object_id, layer->name.c_str());
        }
        diagnostics.recordSceneStage(stage_name, snapshot, snapshot_texture, snapshot_attachment);
        if (!raw_layer) ++layer_index;
#else
        (void)layer;
#endif
    };

    auto draw_layer = [&](Layer* layer) {
        auto* particle = dynamic_cast<ParticleLayer*>(layer);
        if (particle && particle->requiresSceneColor()) {
            particle->setSceneColorView(scene_targets[current].texture_view);
            capture_layer_result(layer, true);
            const int next = 1 - current;
            sg_pass composite_pass = {};
            composite_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
            composite_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
            composite_pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
            composite_pass.attachments.colors[0] = scene_targets[next].attachment_view;
            sg_begin_pass(&composite_pass);

            float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            renderer_draw_sprite(ctx, &ctx.renderer, scene_targets[current].image, scene_targets[current].texture_view,
                                 0.0f, 0.0f, (float)width, (float)height, 0.0f, white, false, nullptr);
            particle->setSceneColorView(scene_targets[current].texture_view);
            particle->draw(ctx);
            sg_end_pass();
            current = next;
            capture_layer_result(layer, false);
            return;
        }

        auto* image = dynamic_cast<ImageLayer*>(layer);
        if (image && image->requiresSceneColor()) {
            if (image->is_fullscreen && !image->effects.empty()) {
                image->renderEffectChain(ctx, scene_targets[current].image, scene_targets[current].texture_view);
            }
            capture_layer_result(layer, true);
            const int next = 1 - current;
            sg_pass composite_pass = {};
            composite_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
            composite_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
            composite_pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};
            composite_pass.attachments.colors[0] = scene_targets[next].attachment_view;
            sg_begin_pass(&composite_pass);
            float white[4] = {1, 1, 1, 1};
            renderer_draw_sprite(ctx, &ctx.renderer, scene_targets[current].image, scene_targets[current].texture_view,
                                 0, 0, (float)width, (float)height, 0, white, false, nullptr);
            image->drawComposite(ctx, scene_targets[current].texture_view);
            sg_end_pass();
            current = next;
            capture_layer_result(layer, false);
            return;
        }

        capture_layer_result(layer, true);

        sg_pass layer_pass = {};
        layer_pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
        layer_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
        layer_pass.attachments.colors[0] = scene_targets[current].attachment_view;
        sg_begin_pass(&layer_pass);
        layer->draw(ctx);
        sg_end_pass();
        capture_layer_result(layer, false);
    };

    if (ctx.test_mode && ctx.selected_object >= 0 && ctx.selected_object < (int)ctx.layers.size()) {
        draw_layer(ctx.layers[ctx.selected_object]);
    } else {
        bool any_solo = false;
        for (auto layer : ctx.layers) {
            if (layer->solo) {
                any_solo = true;
                break;
            }
        }
        for (auto layer : ctx.layers) {
            if ((any_solo && !layer->solo) || (!any_solo && !layer->visible)) continue;
            draw_layer(layer);
        }
    }

    renderBloom(current, width, height);
#if DEBUG_BUILD
    // The layer snapshots above intentionally stop before post-processing.
    // Keep one final stage after bloom so a diagnostic capture represents the
    // image that present() will actually send to the swapchain.
    {
        RenderDiagnostics& diagnostics = RenderDiagnostics::instance();
        if (diagnostics.is_capturing_frame) {
            sg_image_desc image_desc = {};
            image_desc.usage.color_attachment = true;
            image_desc.width = width;
            image_desc.height = height;
            image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
            sg_image snapshot = sg_make_image(&image_desc);
            if (snapshot.id != SG_INVALID_ID) {
                sg_view_desc texture_desc = {};
                texture_desc.texture.image = snapshot;
                sg_view texture_view = sg_make_view(&texture_desc);
                sg_view_desc attachment_desc = {};
                attachment_desc.color_attachment.image = snapshot;
                sg_view attachment_view = sg_make_view(&attachment_desc);
                if (texture_view.id != SG_INVALID_ID && attachment_view.id != SG_INVALID_ID) {
                    sg_pass snapshot_pass = {};
                    snapshot_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
                    snapshot_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
                    snapshot_pass.attachments.colors[0] = attachment_view;
                    sg_begin_pass(&snapshot_pass);
                    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                    renderer_draw_sprite(ctx, &ctx.renderer, scene_targets[current].image,
                                         scene_targets[current].texture_view, 0.0f, 0.0f, (float)width, (float)height,
                                         0.0f, white, false, nullptr);
                    sg_end_pass();
                    diagnostics.recordSceneStage("post-bloom-final", snapshot, texture_view, attachment_view);
                } else {
                    if (texture_view.id != SG_INVALID_ID) sg_destroy_view(texture_view);
                    if (attachment_view.id != SG_INVALID_ID) sg_destroy_view(attachment_view);
                    sg_destroy_image(snapshot);
                }
            }
        }
    }
#endif
    scene_output_index = current;
}

void Scene2DRuntime::draw() {
    if (requiresOffscreenComposition())
        drawOffscreen();
    else {
        scene_output_index = -1;
        drawDirect();
    }
}

void Scene2DRuntime::drawParticleDiagnostics() {
    if (!ctx.particle_debug_bounds && !ctx.particle_debug_velocity) return;
    for (Layer* layer : ctx.layers) {
        if (!layer->visible) continue;
        if (auto* particle = dynamic_cast<ParticleLayer*>(layer)) particle->drawDebug(ctx);
    }
}

void Scene2DRuntime::present() {
    if (scene_output_index < 0 || scene_output_index > 1) return;
    SceneTarget& target = scene_targets[scene_output_index];
    if (target.image.id == SG_INVALID_ID || target.texture_view.id == SG_INVALID_ID) return;

    const bool has_output_viewport = output_width > 0 && output_height > 0;
    const int width = has_output_viewport ? output_width : sapp_width();
    const int height = has_output_viewport ? output_height : sapp_height();
    if (has_output_viewport) {
        sg_apply_viewport(output_x, output_y, width, height, true);
        sg_apply_scissor_rect(output_x, output_y, width, height, true);
    } else {
        sg_apply_viewport(0, 0, width, height, true);
        sg_apply_scissor_rect(0, 0, width, height, true);
    }

    renderer_update_viewport(&ctx.renderer, (float)width, (float)height);
    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    renderer_draw_sprite(ctx, &ctx.renderer, target.image, target.texture_view, 0.0f, 0.0f, (float)width, (float)height,
                         0.0f, white, false, nullptr);

    if (has_output_viewport) {
        sg_apply_viewport(0, 0, sapp_width(), sapp_height(), true);
        sg_apply_scissor_rect(0, 0, sapp_width(), sapp_height(), true);
    }
}

void Scene2DRuntime::updateViewport() {
    float sw = output_width > 0 ? (float)output_width : (float)sapp_width();
    float sh = output_height > 0 ? (float)output_height : (float)sapp_height();
    renderer_update_viewport(&ctx.renderer, sw, sh);

    if (ctx.scene_w == 0 || ctx.scene_h == 0) return;

    float aspect_scene = ctx.scene_w / ctx.scene_h;
    float aspect_window = sw / sh;

    if (ctx.scaling_mode == SCALING_FIT) {
        ctx.render_scale = aspect_window > aspect_scene ? sh / ctx.scene_h : sw / ctx.scene_w;
    } else {
        ctx.render_scale = aspect_window > aspect_scene ? sw / ctx.scene_w : sh / ctx.scene_h;
    }
    // Zoom is part of the authored camera transform, not an editor-only hint.
    ctx.render_scale *= std::max(ctx.general.zoom, 0.001f);

    ctx.offset_x = (sw - ctx.scene_w * ctx.render_scale) * 0.5f;
    ctx.offset_y = (sh - ctx.scene_h * ctx.render_scale) * 0.5f;
}

void Scene2DRuntime::setOutputViewport(int x, int y, int width, int height) {
    output_x = x;
    output_y = y;
    output_width = width;
    output_height = height;
}

void Scene2DRuntime::resetOutputViewport() {
    output_x = 0;
    output_y = 0;
    output_width = 0;
    output_height = 0;
}

void Scene2DRuntime::clearScene() {
    for (auto layer : ctx.layers) delete layer;
    ctx.layers.clear();
    delete ctx.scene_tree;
    ctx.scene_tree = nullptr;
    ctx.selected_object = -1;
    ctx.test_mode = false;
}

void Scene2DRuntime::cleanup() {
    clearScene();
    scene_targets[0] = SceneTarget{};
    scene_targets[1] = SceneTarget{};
    bloom_targets[0] = SceneTarget{};
    bloom_targets[1] = SceneTarget{};
    scene_output_index = -1;
    renderer_cleanup(&ctx.renderer);
}
