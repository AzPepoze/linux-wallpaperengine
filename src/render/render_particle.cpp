#include <algorithm>
#include <cstring>

#include "core/engine_context.h"
#include "render.h"

void renderer_draw_particle_batch(EngineContext& ctx, renderer_t* r, sg_buffer vertex_buffer, sg_buffer index_buffer,
                                  int index_count, sg_image main_image, sg_view main_view,
                                  const render_effect_pass_t* pass, const builtin_uniforms_t& input_builtins,
                                  const particle_builtin_uniforms_t& particle_builtins) {
    if (!r || !pass || !pass->enabled || pass->pipeline.id == SG_INVALID_ID || vertex_buffer.id == SG_INVALID_ID ||
        index_buffer.id == SG_INVALID_ID || main_image.id == SG_INVALID_ID || main_view.id == SG_INVALID_ID ||
        index_count <= 0) {
        return;
    }

    for (int slot = 0; slot < SG_MAX_SAMPLER_BINDSLOTS; ++slot) r->bind.samplers[slot] = r->smp_repeat;

    sg_apply_pipeline(pass->pipeline);
    r->bind.vertex_buffers[0] = vertex_buffer;
    r->bind.index_buffer = index_buffer;
    r->bind.views[0] = main_view;
    r->bind.samplers[0] = pass->repeat_effect_input ? r->smp_repeat : r->smp_clamp;

    builtin_uniforms_t builtins = input_builtins;
    const sg_image_desc main_desc = sg_query_image_desc(main_image);
    builtins.texture_resolutions[0][0] = main_desc.width > 0 ? (float)main_desc.width : 1.0f;
    builtins.texture_resolutions[0][1] = main_desc.height > 0 ? (float)main_desc.height : 1.0f;
    builtins.texture_resolutions[0][2] = builtins.texture_resolutions[0][0];
    builtins.texture_resolutions[0][3] = builtins.texture_resolutions[0][1];

    for (int extra_index = 0; extra_index < 11; ++extra_index) {
        const int slot = extra_index + 1;
        if (pass->override_views && extra_index < (int)pass->num_override_views &&
            pass->override_views[extra_index].id != SG_INVALID_ID) {
            r->bind.views[slot] = pass->override_views[extra_index];
        } else if (pass->extra_views && extra_index < (int)pass->num_extra_views &&
                   pass->extra_views[extra_index].id != SG_INVALID_ID) {
            r->bind.views[slot] = pass->extra_views[extra_index];
        } else if (extra_index == 1) {
            r->bind.views[slot] = r->white_view;
        } else {
            r->bind.views[slot] = r->black_view;
        }

        const sg_image sampled_image = sg_query_view_image(r->bind.views[slot]);
        if (sampled_image.id != SG_INVALID_ID) {
            const sg_image_desc sampled_desc = sg_query_image_desc(sampled_image);
            if (sampled_desc.usage.color_attachment) r->bind.samplers[slot] = r->smp_clamp;
            if (slot < 5) {
                builtins.texture_resolutions[slot][0] = sampled_desc.width > 0 ? (float)sampled_desc.width : 1.0f;
                builtins.texture_resolutions[slot][1] = sampled_desc.height > 0 ? (float)sampled_desc.height : 1.0f;
                builtins.texture_resolutions[slot][2] = builtins.texture_resolutions[slot][0];
                builtins.texture_resolutions[slot][3] = builtins.texture_resolutions[slot][1];
            }
        } else if (slot < 5) {
            builtins.texture_resolutions[slot][0] = 1.0f;
            builtins.texture_resolutions[slot][1] = 1.0f;
            builtins.texture_resolutions[slot][2] = 1.0f;
            builtins.texture_resolutions[slot][3] = 1.0f;
        }
    }

    sg_range mvp_range = SG_RANGE(builtins.mvp);
    sg_apply_uniforms(0, &mvp_range);

    constexpr size_t kBuiltinRestSize = sizeof(builtin_uniforms_t) - sizeof(mat4x4);
    const uint8_t* builtin_rest = reinterpret_cast<const uint8_t*>(&builtins) + sizeof(mat4x4);
    sg_range vertex_builtin_range = {.ptr = builtin_rest, .size = kBuiltinRestSize};
    sg_apply_uniforms(1, &vertex_builtin_range);

    alignas(16) uint8_t fragment_uniforms[kBuiltinRestSize + sizeof(float) * 4] = {};
    memcpy(fragment_uniforms, builtin_rest, kBuiltinRestSize);
    const float white_tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    memcpy(fragment_uniforms + kBuiltinRestSize, white_tint, sizeof(white_tint));
    sg_range fragment_range = {.ptr = fragment_uniforms, .size = sizeof(fragment_uniforms)};
    sg_apply_uniforms(2, &fragment_range);

    sg_range particle_range = {.ptr = &particle_builtins, .size = sizeof(particle_builtins)};
    sg_apply_uniforms(3, &particle_range);

    sg_apply_bindings(&r->bind);
    if (pass->apply_custom_uniforms) pass->apply_custom_uniforms(pass->user_data);
    sg_draw(0, index_count, 1);
    r->draw_calls++;

    for (int slot = 0; slot < 12; ++slot) r->bind.views[slot] = {SG_INVALID_ID};
    r->bind.vertex_buffers[0] = r->vertex_buffer;
    r->bind.index_buffer = r->index_buffer;
}
