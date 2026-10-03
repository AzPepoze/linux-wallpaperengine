#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "image_layer.h"
#include "image_parser.h"
#include "shared/core/context.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/core/utils.h"
#include "shared/graphics/backend/gpu_debug_labels.h"
#include "shared/graphics/diagnostics/render_observer.h"
#include "shared/graphics/render.h"
#include "wallpaper/2d/alpha_curve.h"
#include "wallpaper/2d/tree/scene_tree.h"

namespace {
bool readFileBytes(const char* path, std::vector<uint8_t>& out) {
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0) {
        fclose(file);
        return false;
    }
    out.resize((size_t)size);
    const size_t read = fread(out.data(), 1, out.size(), file);
    fclose(file);
    return read == out.size();
}
}  // namespace

void ImageLayer::loadPuppet(const char* mdl_rel_path, EngineContext& ctx) {
    char abs_path[1024];
    if (!ctx.asset_mgr.resolvePath(mdl_rel_path, abs_path, sizeof(abs_path))) return;
    std::vector<uint8_t> bytes;
    if (!readFileBytes(abs_path, bytes)) return;

    wallpaper_engine::MdlModel model;
    if (!wallpaper_engine::parseMdl(bytes.data(), bytes.size(), model)) {
        LOG_TAG_W("PUPPET", "Failed to parse puppet mesh %s", mdl_rel_path);
        return;
    }
    puppet = std::move(model);
    has_puppet_mesh = !puppet.vertices.empty() && !puppet.triangles.empty();
    if (!has_puppet_mesh) return;

    std::vector<float> uvs;
    uvs.reserve(puppet.vertices.size() * 2);
    for (const wallpaper_engine::MdlVertex& vertex : puppet.vertices) {
        uvs.push_back(vertex.uv[0]);
        uvs.push_back(vertex.uv[1]);
    }
    puppet_pose.init(puppet);
    setPuppetLayers();

    sg_buffer_desc uv_desc = {};
    uv_desc.usage.vertex_buffer = true;
    uv_desc.data = {uvs.data(), uvs.size() * sizeof(float)};
    puppet_uv_buffer = sg_make_buffer(&uv_desc);

    std::vector<uint16_t> indices;
    indices.reserve(puppet.triangles.size() * 3);
    for (const wallpaper_engine::MdlTriangle& triangle : puppet.triangles) {
        indices.push_back(triangle.a);
        indices.push_back(triangle.b);
        indices.push_back(triangle.c);
    }
    sg_buffer_desc index_desc = {};
    index_desc.usage.index_buffer = true;
    index_desc.data = {indices.data(), indices.size() * sizeof(uint16_t)};
    puppet_index_buffer = sg_make_buffer(&index_desc);
    puppet_index_count = (int)indices.size();

    sg_buffer_desc position_desc = {};
    position_desc.size = puppet.vertices.size() * 3 * sizeof(float);
    position_desc.usage.vertex_buffer = true;
    position_desc.usage.stream_update = true;
    puppet_position_buffer = sg_make_buffer(&position_desc);
    LOG_TAG_I("PUPPET", "Loaded puppet mesh %s (%zu vertices, %zu triangles, %zu bones)", mdl_rel_path,
              puppet.vertices.size(), puppet.triangles.size(), puppet.bones.size());
}

void ImageLayer::setPuppetLayers() {
    puppet_layers.clear();
    for (const wallpaper_engine::AnimationLayerDocument& entry : alpha_document.animation_layers) {
        wallpaper_engine::PuppetAnimationLayer layer;
        layer.animation_id = entry.animation;
        layer.rate = entry.rate;
        layer.blend = entry.blend;
        layer.additive = entry.additive;
        layer.visible = entry.visible;
        puppet_layers.push_back(layer);
    }
}

bool ImageLayer::ensurePuppetTarget(int width, int height) {
    if (puppet_target.image.id != SG_INVALID_ID && puppet_target.width == width && puppet_target.height == height) {
        return true;
    }
    return puppet_target.create(width, height) && puppet_straight.create(width, height);
}

void ImageLayer::updatePuppetPositions(int width, int height) {
    puppet_pose.skin(puppet, puppet_layers, puppet_skinned);
    puppet_positions.resize(puppet_skinned.size());
    const float center_x = (float)width * 0.5f;
    const float center_y = (float)height * 0.5f;
    for (size_t i = 0; i < puppet_skinned.size(); i += 3) {
        puppet_positions[i + 0] = center_x + puppet_skinned[i + 0];
        puppet_positions[i + 1] = center_y - puppet_skinned[i + 1];
        puppet_positions[i + 2] = 0.0f;
    }
    if (puppet_position_buffer.id == SG_INVALID_ID) return;
    sg_range range = {puppet_positions.data(), puppet_positions.size() * sizeof(float)};
    sg_update_buffer(puppet_position_buffer, &range);
}

bool ImageLayer::renderPuppet(EngineContext& ctx) {
    if (!has_puppet_mesh || img.id == SG_INVALID_ID) return false;
    if (cached_view.id == SG_INVALID_ID) updateCachedView();
    if (cached_view.id == SG_INVALID_ID) return false;

    const bool has_effect_output = effect_output_image.id != SG_INVALID_ID && effect_output_view.id != SG_INVALID_ID;
    const sg_image source_image = has_effect_output ? effect_output_image : (sg_image)img;
    const sg_view source_view = has_effect_output ? effect_output_view : (sg_view)cached_view;

    const sg_image_desc material_desc = sg_query_image_desc(img);
    int width = (int)std::lround(size[0] > 0.0f ? size[0] : (float)material_desc.width);
    int height = (int)std::lround(size[1] > 0.0f ? size[1] : (float)material_desc.height);
    width = std::max(1, width);
    height = std::max(1, height);
    if (!ensurePuppetTarget(width, height)) return false;

    updatePuppetPositions(width, height);

    const float saved_view_width = ctx.renderer.view_width;
    const float saved_view_height = ctx.renderer.view_height;

    sg_pass pass = {};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
    pass.attachments.colors[0] = puppet_target.attachment_view;
    sg_begin_pass(&pass);
    renderer_update_viewport(&ctx.renderer, (float)width, (float)height);
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    renderer_draw_mesh(ctx, &ctx.renderer, puppet_position_buffer, puppet_uv_buffer, puppet_index_buffer,
                       puppet_index_count, source_image, source_view, white, (float)width, (float)height);
    sg_end_pass();

    sg_pass resolve_pass = {};
    resolve_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    resolve_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    resolve_pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
    resolve_pass.attachments.colors[0] = puppet_straight.attachment_view;
    sg_begin_pass(&resolve_pass);
    renderer_update_viewport(&ctx.renderer, (float)width, (float)height);
    renderer_draw_unpremultiplied(&ctx.renderer, puppet_target.texture_view, (float)width, (float)height);
    sg_end_pass();

    renderer_update_viewport(&ctx.renderer, saved_view_width, saved_view_height);
    return true;
}
