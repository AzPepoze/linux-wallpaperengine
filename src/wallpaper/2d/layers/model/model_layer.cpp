#include "model_layer.h"

#include <algorithm>
#include <cmath>

#include "shared/assets/asset_manager.h"
#include "shared/core/engine_context.h"
#include "shared/core/logger.h"
#include "shared/graphics/render.h"

namespace {
constexpr int kMaxSize = 4096;

GfxImage whitePixel() {
    const uint32_t white = 0xFFFFFFFFu;
    sg_image_desc desc = {};
    desc.width = 1;
    desc.height = 1;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0] = {&white, sizeof(white)};
    return GfxImage(sg_make_image(&desc));
}

GfxBuffer makeBuffer(size_t bytes, bool index) {
    sg_buffer_desc desc = {};
    desc.size = bytes;
    desc.usage.stream_update = true;
    if (index)
        desc.usage.index_buffer = true;
    else
        desc.usage.vertex_buffer = true;
    return GfxBuffer(sg_make_buffer(&desc));
}

void writeBuffer(GfxBuffer& buffer, size_t& current_bytes, const void* data, size_t bytes, bool index) {
    if (bytes == 0) {
        buffer = GfxBuffer();
        current_bytes = 0;
        return;
    }
    if (current_bytes != bytes || buffer.id == SG_INVALID_ID) {
        buffer = makeBuffer(bytes, index);
        current_bytes = bytes;
    }
    sg_range range = {data, bytes};
    sg_update_buffer(buffer, &range);
}
}  // namespace

ModelLayer::ModelLayer(const char* name, std::shared_ptr<ModelData> data)
    : ImageLayer(name, whitePixel()), data_(std::move(data)) {}

void ModelLayer::upload(EngineContext& ctx) {
    uploaded_revision_ = data_->revision;
    buffers_.resize(data_->shapes.size());

    // The picture is centered on the layer origin and spans the geometry in both directions.
    float half_width = 1.0f;
    float half_height = 1.0f;
    for (const ModelShape& shape : data_->shapes) {
        const int stride = shape.stride();
        const int position = shape.offsetOf(VertexAttribute::Position);
        if (stride <= 0 || position < 0) continue;
        for (size_t i = 0; i + (size_t)stride <= shape.vertices.size(); i += (size_t)stride) {
            half_width = std::max(half_width, std::fabs(shape.vertices[i + (size_t)position] + shape.origin[0]));
            half_height = std::max(half_height, std::fabs(shape.vertices[i + (size_t)position + 1] + shape.origin[1]));
        }
    }
    width_ = std::clamp((int)std::ceil(half_width * 2.0f), 1, kMaxSize);
    height_ = std::clamp((int)std::ceil(half_height * 2.0f), 1, kMaxSize);
    const float center_x = (float)width_ * 0.5f;
    const float center_y = (float)height_ * 0.5f;

    for (size_t s = 0; s < data_->shapes.size(); ++s) {
        const ModelShape& shape = data_->shapes[s];
        ShapeBuffers& gpu = buffers_[s];
        const int stride = shape.stride();
        const int position = shape.offsetOf(VertexAttribute::Position);
        const int uv = shape.offsetOf(VertexAttribute::Uv);
        const size_t count = stride > 0 && position >= 0 ? shape.vertices.size() / (size_t)stride : 0;

        std::vector<float> positions(count * 3), uvs(count * 2);
        for (size_t v = 0; v < count; ++v) {
            const float* vertex = &shape.vertices[v * (size_t)stride];
            positions[v * 3 + 0] = center_x + vertex[position] + shape.origin[0];
            positions[v * 3 + 1] = center_y - (vertex[position + 1] + shape.origin[1]);
            positions[v * 3 + 2] = 0.0f;
            uvs[v * 2 + 0] = uv >= 0 ? vertex[uv] : 0.0f;
            uvs[v * 2 + 1] = uv >= 0 ? vertex[uv + 1] : 0.0f;
        }

        std::vector<uint16_t> indices;
        if (count > 65535) {
            LOG_W("Model shape %zu has %zu vertices; 16-bit indices cap a shape at 65535", s, count);
        } else if (shape.indices.empty()) {
            for (size_t i = 0; i + 2 < count; i += 3) {
                indices.push_back((uint16_t)i);
                indices.push_back((uint16_t)(i + 1));
                indices.push_back((uint16_t)(i + 2));
            }
        } else {
            for (uint32_t index : shape.indices)
                if (index < count) indices.push_back((uint16_t)index);
            indices.resize(indices.size() - indices.size() % 3);
        }

        writeBuffer(gpu.positions, gpu.position_bytes, positions.data(), positions.size() * sizeof(float), false);
        writeBuffer(gpu.uvs, gpu.uv_bytes, uvs.data(), uvs.size() * sizeof(float), false);
        writeBuffer(gpu.indices, gpu.index_bytes, indices.data(), indices.size() * sizeof(uint16_t), true);
        gpu.index_count = indices.size();

        if (gpu.material != shape.material || gpu.texture_view.id == SG_INVALID_ID) {
            gpu.material = shape.material;
            gpu.texture = {};
            gpu.texture_view = {};
            if (!shape.material.empty()) gpu.texture = ctx.asset_mgr->resolveMaterialTexture(shape.material.c_str());
            if (gpu.texture.id != SG_INVALID_ID) {
                sg_view_desc view_desc = {};
                view_desc.texture.image = gpu.texture;
                gpu.texture_view = sg_make_view(&view_desc);
            }
        }
    }
}

void ModelLayer::update(float dt, EngineContext& ctx) {
    ImageLayer::update(dt, ctx);
    if (!data_) return;
    if (data_->revision != uploaded_revision_) upload(ctx);

    size[0] = (float)width_;
    size[1] = (float)height_;
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    renderGeometry(ctx, width_, height_, [&] {
        for (const ShapeBuffers& gpu : buffers_) {
            if (gpu.index_count == 0) continue;
            const bool textured = gpu.texture_view.id != SG_INVALID_ID && gpu.texture.id != SG_INVALID_ID;
            const sg_view view = textured ? (sg_view)gpu.texture_view : (sg_view)ctx.renderer.white_view;
            const sg_image image = textured ? (sg_image)gpu.texture : (sg_image)ctx.renderer.white_pixel;
            renderer_draw_mesh(ctx, &ctx.renderer, gpu.positions, gpu.uvs, gpu.indices, (int)gpu.index_count, image,
                               view, white, (float)width_, (float)height_);
        }
    });
}
