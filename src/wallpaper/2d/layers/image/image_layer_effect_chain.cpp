#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "image_layer.h"
#include "image_parser.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/backend/gpu_debug_labels.h"
#include "shared/graphics/backend/gpu_timing.h"
#include "shared/graphics/diagnostics/render_observer.h"
#include "shared/graphics/pass_util.h"
#include "shared/graphics/passes/shader_pass.h"
#include "shared/graphics/render.h"
#include "wallpaper/2d/alpha_curve.h"
#include "wallpaper/2d/effects/effect.h"
#include "wallpaper/2d/tree/scene_tree.h"

namespace {
// Effect-target pixels added around the cropped region when drawing it, so bilinear edge taps stay inside.
constexpr float kOutputRegionPadding = 2.0f;
bool isCompositeRenderTarget(const std::string& name) {
    if (name.rfind("_rt_", 0) != 0) return false;
    if (name == "_rt_FullFrameBuffer") return true;
    if (name.rfind("_rt_imageLayerComposite_", 0) == 0) return true;
    return name.find("FrameBuffer") != std::string::npos;
}

// `exact` replaces the target texel for texel instead of alpha-blending onto transparent black.
void copyInputToTarget(EngineContext& ctx, sg_image input_image, sg_view input_view, sg_view target, int width,
                       int height, bool exact = false) {
    sg_pass copy_pass = colorPass(target, SG_LOADACTION_CLEAR);
    sg_begin_pass(&copy_pass);
    renderer_update_viewport(&ctx.renderer, (float)width, (float)height);
    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    renderer_draw_sprite(ctx, &ctx.renderer, input_image, input_view, 0.0f, 0.0f, (float)width, (float)height, 0.0f,
                         white, false, nullptr, exact);
    sg_end_pass();
}

bool aliasesOutput(const std::string& layer_name, const ShaderPass& pass, sg_image slot0_image,
                   const std::array<sg_image, 11>& override_images, const render_effect_pass_t& render_pass,
                   sg_image output_image) {
    if (output_image.id == SG_INVALID_ID) return false;
    bool aliased = false;
    if (slot0_image.id == output_image.id) {
        effect_log.error(
            "Effect pass '%s' (layer '%s') aliases input slot 0 (image %u) with output attachment (image %u)",
            pass.shader_name.c_str(), layer_name.c_str(), slot0_image.id, output_image.id);
        aliased = true;
    }
    for (size_t i = 0; i < override_images.size(); ++i) {
        if (override_images[i].id != output_image.id) continue;
        effect_log.error(
            "Effect pass '%s' (layer '%s') aliases input slot %zu (image %u) with output attachment (image %u)",
            pass.shader_name.c_str(), layer_name.c_str(), i + 1, override_images[i].id, output_image.id);
        aliased = true;
    }
    for (size_t i = 0; i < (size_t)render_pass.num_extra_views; ++i) {
        if (render_pass.override_views && i < render_pass.num_override_views &&
            render_pass.override_views[i].id != SG_INVALID_ID) {
            continue;
        }
        if (!render_pass.extra_views || render_pass.extra_views[i].id == SG_INVALID_ID) continue;
        const sg_view_desc view_desc = sg_query_view_desc(render_pass.extra_views[i]);
        if (view_desc.texture.image.id != SG_INVALID_ID && view_desc.texture.image.id == output_image.id) {
            effect_log.error(
                "Effect pass '%s' (layer '%s') aliases extra view slot %zu (image %u) with output attachment "
                "(image %u)",
                pass.shader_name.c_str(), layer_name.c_str(), i + 1, view_desc.texture.image.id, output_image.id);
            aliased = true;
        }
    }
    return aliased;
}
}  // namespace

ImageLayer::PassInputs ImageLayer::resolvePassInputs(const ShaderPass& pass, const ChainState& state) {
    PassInputs inputs;
    inputs.image = state.input_image;
    inputs.view = state.input_view;
    if (pass.pass_textures.texture0.id != SG_INVALID_ID && pass.pass_textures.texture0_view.id != SG_INVALID_ID) {
        inputs.image = pass.pass_textures.texture0;
        inputs.view = pass.pass_textures.texture0_view;
    }
    inputs.override_images.fill(sg_image{SG_INVALID_ID});
    inputs.override_views.fill(sg_view{SG_INVALID_ID});

    for (const auto& [slot, binding] : pass.render_texture_bindings) {
        if (slot < 0 || slot > 11) continue;
        sg_image binding_image = {SG_INVALID_ID};
        sg_view binding_view = {SG_INVALID_ID};
        if (binding == "previous") {
            binding_image = state.chain_image;
            binding_view = state.chain_view;
        } else if (auto target = named_effect_targets.find(binding); target != named_effect_targets.end()) {
            const auto& read_buf = target->second.currentRead();
            binding_image = read_buf.image;
            binding_view = read_buf.texture_view;
        } else if (isCompositeRenderTarget(binding)) {
            // Unwritten composite targets read as the accumulated scene image.
            binding_image = state.layer_source_image;
            binding_view = state.layer_source_view;
        } else {
            continue;
        }
        if (binding_image.id == SG_INVALID_ID) continue;
        if (slot == 0) {
            inputs.image = binding_image;
            inputs.view = binding_view;
        } else {
            inputs.override_images[slot - 1] = binding_image;
            inputs.override_views[slot - 1] = binding_view;
        }
        inputs.has_overrides = true;
    }
    return inputs;
}

void ImageLayer::advanceChain(ChainState& state, NamedRenderTarget* named_target) {
    if (named_target) {
        named_target->swap();
        state.input_image = named_target->currentRead().image;
        state.input_view = named_target->currentRead().texture_view;
    } else {
        state.input_image = effect_targets[state.write_index].image;
        state.input_view = effect_targets[state.write_index].texture_view;
        state.chain_image = state.input_image;
        state.chain_view = state.input_view;
        state.write_index = 1 - state.write_index;
    }
    effect_output_image = state.input_image;
    effect_output_view = state.input_view;
    state.rendered_any = true;
}

void ImageLayer::tracePass(IRenderObserver& diag, EngineContext& ctx, const Effect& effect, const ShaderPass& pass,
                           int effect_index, int pass_index, const PassInputs& inputs, ChainState& state,
                           NamedRenderTarget* named_target, sg_image output_image, int target_width,
                           int target_height) {
    PassTraceEntry trace;
    trace.frame_number = ctx.profiler.frame_index;
    trace.layer_name = name.empty() ? ("Layer_" + std::to_string(scene_object_id)) : name;
    trace.effect_index = effect_index;
    trace.effect_file = effect.file_path;
    trace.pass_index = pass_index;
    trace.shader_name = pass.shader_name;
    trace.enabled = pass.enabled;
    trace.visible = effect.visible;
    trace.draw_order = state.draw_order++;
    trace.render_target_name = pass.render_target;
    trace.target_image_id = output_image.id;
    trace.target_view_id = named_target ? named_target->currentWrite().attachment_view.id
                                        : effect_targets[state.write_index].attachment_view.id;
    trace.target_width = target_width;
    trace.target_height = target_height;
    trace.target_pixel_format = "RGBA8";
    trace.render_scale = pass.render_scale;
    trace.is_fullscreen_quad = pass.is_fullscreen_quad;

    auto describe = [](TextureBindingTrace& binding, sg_image image, sg_view view) {
        binding.image_id = image.id;
        binding.view_id = view.id;
        const sg_image_desc desc = sg_query_image_desc(image);
        binding.width = desc.width;
        binding.height = desc.height;
        binding.is_render_target = desc.usage.color_attachment;
    };
    auto describeNamedTarget = [](TextureBindingTrace& binding, const NamedRenderTarget& target) {
        const auto& read_buf = target.currentRead();
        binding.image_id = read_buf.image.id;
        binding.view_id = read_buf.texture_view.id;
        binding.width = read_buf.width;
        binding.height = read_buf.height;
        binding.is_render_target = true;
    };

    TextureBindingTrace slot0;
    slot0.slot = 0;
    describe(slot0, inputs.image, inputs.view);
    slot0.pixel_format = "RGBA8";
    slot0.semantic_source = pass.render_texture_bindings.count(0) ? pass.render_texture_bindings.at(0) : "previous";
    trace.inputs.push_back(slot0);

    for (const auto& [slot, binding] : pass.render_texture_bindings) {
        if (slot == 0) continue;
        TextureBindingTrace input;
        input.slot = slot;
        input.semantic_source = binding;
        const auto named = named_effect_targets.find(binding);
        if (binding == "previous") {
            describe(input, state.chain_image, state.chain_view);
        } else if (named != named_effect_targets.end()) {
            describeNamedTarget(input, named->second);
        } else if (isCompositeRenderTarget(binding)) {
            describe(input, state.layer_source_image, state.layer_source_view);
        }
        trace.inputs.push_back(input);
    }

    gpu_set_image_debug_label(output_image, (pass.shader_name + " Target").c_str());
    diag.recordPass(trace, output_image);
}

const content_bounds::Rect& ImageLayer::passActiveRegion(EngineContext& ctx, const ShaderPass& pass) {
    const auto cached = pass_active_regions.find(&pass);
    if (cached != pass_active_regions.end()) return cached->second;

    // waterwaves leaves the input untouched where the mask is zero; the mask shares UVs only at the layer's aspect.
    content_bounds::Rect region;
    // The opacity mask is g_Texture1, which the pass stores as texture 0 of its extra textures.
    const PassTextures& textures = pass.pass_textures;
    const bool masked_waves = pass.shader_name.size() >= 10 &&
                              pass.shader_name.compare(pass.shader_name.size() - 10, 10, "waterwaves") == 0 &&
                              !textures.texture_paths.empty() && !textures.texture_paths[0].empty() &&
                              !textures.textures.empty() && textures.textures[0].id != SG_INVALID_ID;
    if (masked_waves && effect_target_width > 0 && effect_target_height > 0) {
        const sg_image_desc mask_desc = sg_query_image_desc(textures.textures[0]);
        const double mask_aspect = (double)mask_desc.width / std::max(1, mask_desc.height);
        const double layer_aspect = (double)effect_target_width / effect_target_height;
        if (mask_desc.width > 0 && std::abs(mask_aspect / layer_aspect - 1.0) < 0.01) {
            region = ctx.asset_mgr->textureContentBounds(textures.texture_paths[0].c_str());
        }
    }
    return pass_active_regions.emplace(&pass, region).first->second;
}

bool ImageLayer::effectChainCanCrop() const {
    if (!source_content.valid) return false;
    for (const Effect* effect : effects) {
        if (!effect || !effect->visible) continue;
        for (const ShaderPass* pass : effect->passes) {
            if (!pass || !pass->enabled) continue;
            if (!pass->render_target.empty() || !content_bounds::passDisplacement(pass->shader_name, pass->uniforms))
                return false;
        }
    }
    return true;
}

uint64_t ImageLayer::effectChainSignature(EngineContext& ctx, sg_image base_image, sg_view base_view) {
    uint64_t hash = UINT64_C(14695981039346656037);
    auto mix = [&hash](const void* data, size_t size) {
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    };
    auto mixValue = [&mix](auto value) { mix(&value, sizeof(value)); };

    mixValue(base_image.id);
    mixValue(base_view.id);
    mixValue(effect_target_width);
    mixValue(effect_target_height);
    mixValue(effect_targets[0].image.id);
    mixValue(effect_targets[1].image.id);
    for (Effect* effect : effects) {
        if (!effect) continue;
        mixValue(effect->visible);
        for (ShaderPass* pass : effect->passes) {
            if (!pass) continue;
            mixValue(pass->enabled);
            if (!pass->enabled) continue;
            if (pass->frame_varying) return 0;
            // Keyframed uniforms are evaluated first, so their current values are what gets fingerprinted.
            pass->getRenderPass(ctx.profiler.frame_index, ctx.scene.elapsed_time);
            for (const auto& [slot, binding] : pass->render_texture_bindings) {
                if (isCompositeRenderTarget(binding)) return 0;
            }
            mixValue(pass->compiled.pipeline.id);
            mixValue(pass->pass_textures.texture0.id);
            for (const GfxView& view : pass->pass_textures.cached_views) mixValue(view.id);
            for (const auto& [uniform_name, values] : pass->uniforms) {
                mix(uniform_name.data(), uniform_name.size());
                mix(values.data(), values.size() * sizeof(float));
            }
        }
    }
    return hash | 1;
}

void ImageLayer::renderEffectChain(EngineContext& ctx, sg_image src_img, sg_view src_view) {
    const sg_image previous_output_image = effect_output_image;
    const sg_view previous_output_view = effect_output_view;
    effect_output_image = {SG_INVALID_ID};
    effect_output_view = {SG_INVALID_ID};
    sg_image base_img = src_img.id != SG_INVALID_ID
                            ? src_img
                            : (current_texture_frame ? (sg_image)animated_frame.image : (sg_image)img);
    sg_view base_view = src_view.id != SG_INVALID_ID
                            ? src_view
                            : (current_texture_frame ? (sg_view)animated_frame.texture_view : (sg_view)cached_view);
    if (effects.empty() || base_img.id == SG_INVALID_ID) return;
    if (base_view.id == SG_INVALID_ID) {
        if (src_img.id != SG_INVALID_ID) {
            sg_view_desc v_desc = {};
            v_desc.texture.image = src_img;
            base_view = sg_make_view(&v_desc);
        } else {
            updateCachedView();
            base_view = cached_view;
        }
    }
    effect_source_image = base_img;
    effect_source_view = base_view;
    if (base_view.id == SG_INVALID_ID || !ensureEffectTargets(ctx, base_img)) return;

    IRenderObserver& diag = renderObserver();

    // Skip the chain when its inputs are unchanged and no pass reads time, pointer, parallax or audio.
    const bool plain_source = src_img.id == SG_INVALID_ID && !effectSourceIsDynamic();
    const bool static_source = plain_source && !diag.isCapturingFrame() && !diag.isTracingPasses();
    const uint64_t signature = static_source ? effectChainSignature(ctx, base_img, base_view) : 0;
    if (signature != 0 && signature == effect_chain_signature && previous_output_image.id != SG_INVALID_ID &&
        previous_output_view.id != SG_INVALID_ID) {
        effect_output_image = previous_output_image;
        effect_output_view = previous_output_view;
        return;
    }
    effect_chain_signature = 0;
    output_region = {};

    const bool any_effect_solo =
        std::any_of(effects.begin(), effects.end(), [](const Effect* effect) { return effect && effect->solo; });

    // Crop-safe passes keep transparent texels transparent, so the region grows only by each pass's reach.
    bool crop = static_source && effectChainCanCrop();
    content_bounds::Rect reach = source_content;

    ChainState state;
    state.layer_source_image = state.input_image = state.chain_image = base_img;
    state.layer_source_view = state.input_view = state.chain_view = base_view;
    const sg_image_desc source_desc = sg_query_image_desc(state.input_image);
    diag.onSourceImage(0, state.input_image, source_desc.width, source_desc.height);

    const float saved_view_width = ctx.renderer.view_width;
    const float saved_view_height = ctx.renderer.view_height;
    renderer_update_viewport(&ctx.renderer, (float)effect_target_width, (float)effect_target_height);

    for (int eff_idx = 0; eff_idx < (int)effects.size(); ++eff_idx) {
        Effect* effect = effects[eff_idx];
        if (!effect) continue;
        if (!effect->visible || (any_effect_solo && !effect->solo)) continue;
        if (!diag.isEffectIsolated(eff_idx, effect->file_path)) continue;
        if (diag.isEffectDisabled(eff_idx, effect->file_path)) continue;

        for (int pass_idx = 0; pass_idx < (int)effect->passes.size(); ++pass_idx) {
            ShaderPass* pass = effect->passes[pass_idx];
            if (!pass || !pass->enabled) continue;

            if (pass->shader_name.find("depthparallax") != std::string::npos && !path.empty() &&
                strstr(path.c_str(), ".tex")) {
                pass->resolveDepth(path.c_str(), ctx);
            }

            int target_width = effect_target_width;
            int target_height = effect_target_height;
            NamedRenderTarget* named_target = nullptr;
            if (!pass->render_target.empty()) {
                target_width = std::max(1, (int)std::lround(effect_target_width / pass->render_scale));
                target_height = std::max(1, (int)std::lround(effect_target_height / pass->render_scale));
                auto& target = named_effect_targets[pass->render_target];
                if (!target.ensureSize(target_width, target_height, pass->render_target)) continue;
                named_target = &target;
            }

            if (diag.isPassDisabled(pass_idx)) {
                if (named_target) {
                    // Copy the input through so downstream passes do not sample an uninitialised buffer.
                    copyInputToTarget(ctx, state.input_image, state.input_view,
                                      named_target->currentWrite().attachment_view, target_width, target_height);
                    named_target->swap();
                }
                continue;
            }

            const sg_image output_image =
                named_target ? named_target->currentWrite().image : effect_targets[state.write_index].image;
            const sg_view output_attachment = named_target ? named_target->currentWrite().attachment_view
                                                           : effect_targets[state.write_index].attachment_view;

            if (pass->compiled.pipeline.id == SG_INVALID_ID) {
                // A pass that never compiled forwards its input, so no later pass reads uninitialised targets.
                copyInputToTarget(ctx, state.input_image, state.input_view, output_attachment, target_width,
                                  target_height);
                advanceChain(state, named_target);
                continue;
            }

            PassInputs inputs = resolvePassInputs(*pass, state);
            render_effect_pass_t render_pass = pass->getRenderPass(ctx.profiler.frame_index, ctx.scene.elapsed_time);
            if (inputs.has_overrides) {
                render_pass.override_views = inputs.override_views.data();
                render_pass.num_override_views = inputs.override_views.size();
            }
            if (aliasesOutput(name, *pass, inputs.image, inputs.override_images, render_pass, output_image)) {
                effect_log.warn("Skipping pass '%s' to avoid Vulkan render target aliasing hazard",
                                pass->shader_name.c_str());
                continue;
            }

            if (crop) {
                const std::optional<float> displacement =
                    content_bounds::passDisplacement(pass->shader_name, pass->uniforms);
                if (displacement)
                    reach = content_bounds::expand(reach, *displacement);
                else
                    crop = false;
            }

            const int gpu_token = ctx.performance_profile
                                      ? gpu_timing_begin_pass(name + "/" + std::to_string(eff_idx) + "/" +
                                                              std::to_string(pass_idx) + "/" + pass->shader_name)
                                      : -1;
            // A masked pass over an opaque layer only changes the mask region; copy the rest through.
            const content_bounds::Rect* active_region = nullptr;
            if (!crop && plain_source && source_opaque && !named_target && !state.rendered_any) {
                const content_bounds::Rect& region = passActiveRegion(ctx, *pass);
                if (region.valid) active_region = &region;
            }
            if (active_region) {
                copyInputToTarget(ctx, inputs.image, inputs.view, output_attachment, target_width, target_height, true);
            }
            sg_pass offscreen_pass =
                colorPass(output_attachment, active_region ? SG_LOADACTION_LOAD : SG_LOADACTION_CLEAR);
            sg_begin_pass(&offscreen_pass);
            renderer_update_viewport(&ctx.renderer, (float)target_width, (float)target_height);
            if (active_region) {
                const content_bounds::PixelRect area =
                    content_bounds::toPixels(*active_region, target_width, target_height);
                if (area.width > 0 && area.height > 0)
                    sg_apply_scissor_rect(area.x, area.y, area.width, area.height, true);
            }
            if (crop) {
                const content_bounds::PixelRect area = content_bounds::toPixels(reach, target_width, target_height);
                if (area.width > 0 && area.height > 0)
                    sg_apply_scissor_rect(area.x, area.y, area.width, area.height, true);
            }
            render_pass.is_fullscreen_quad = pass->is_fullscreen_quad;
            render_pass.logical_scale = effect_logical_scale;
            float effect_tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            renderer_draw_sprite(ctx, &ctx.renderer, inputs.image, inputs.view, 0.0f, 0.0f, (float)target_width,
                                 (float)target_height, 0.0f, effect_tint, false, &render_pass);
            sg_end_pass();
            if (ctx.performance_profile) gpu_timing_end_pass(gpu_token);

            if (diag.isTracingPasses()) {
                tracePass(diag, ctx, *effect, *pass, eff_idx, pass_idx, inputs, state, named_target, output_image,
                          target_width, target_height);
            }

            advanceChain(state, named_target);
            if (diag.shouldStopAfterPass(pass_idx)) break;
        }
    }

    renderer_update_viewport(&ctx.renderer, saved_view_width, saved_view_height);
    if (!state.rendered_any) {
        effect_output_image = {SG_INVALID_ID};
        effect_output_view = {SG_INVALID_ID};
    } else {
        diag.onLayerFinalImage(0, effect_output_image, effect_target_width, effect_target_height);
        effect_chain_signature = signature;
        if (crop) {
            output_region = content_bounds::expand(
                reach, kOutputRegionPadding / (float)std::max(1, std::min(effect_target_width, effect_target_height)));
            updateOutputQuad(ctx);
        }
    }
}
