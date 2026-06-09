#ifndef GFX_RESOURCE_H
#define GFX_RESOURCE_H

#include "../../libs/sokol/sokol_gfx.h"

// RAII wrappers for Sokol GPU resources.
// Move-only, automatically destroy on scope exit.
// Implicitly convertible to/from raw Sokol types so existing code
// that accesses .id or passes to sg_* APIs continues to work.

struct GfxImage {
    uint32_t id = SG_INVALID_ID;
    GfxImage() = default;
    GfxImage(sg_image h) : id(h.id) {}
    ~GfxImage() { if (id != SG_INVALID_ID) sg_destroy_image({id}); }
    GfxImage(GfxImage&& o) noexcept : id(o.id) { o.id = SG_INVALID_ID; }
    GfxImage& operator=(GfxImage&& o) noexcept {
        if (this != &o) {
            if (id != SG_INVALID_ID) sg_destroy_image({id});
            id = o.id;
            o.id = SG_INVALID_ID;
        }
        return *this;
    }
    GfxImage(const GfxImage&) = delete;
    GfxImage& operator=(const GfxImage&) = delete;
    operator sg_image() const { return {id}; }
};

struct GfxView {
    uint32_t id = SG_INVALID_ID;
    GfxView() = default;
    GfxView(sg_view h) : id(h.id) {}
    ~GfxView() { if (id != SG_INVALID_ID) sg_destroy_view({id}); }
    GfxView(GfxView&& o) noexcept : id(o.id) { o.id = SG_INVALID_ID; }
    GfxView& operator=(GfxView&& o) noexcept {
        if (this != &o) {
            if (id != SG_INVALID_ID) sg_destroy_view({id});
            id = o.id;
            o.id = SG_INVALID_ID;
        }
        return *this;
    }
    GfxView(const GfxView&) = delete;
    GfxView& operator=(const GfxView&) = delete;
    operator sg_view() const { return {id}; }
};

struct GfxPipeline {
    uint32_t id = SG_INVALID_ID;
    GfxPipeline() = default;
    GfxPipeline(sg_pipeline h) : id(h.id) {}
    ~GfxPipeline() { if (id != SG_INVALID_ID) sg_destroy_pipeline({id}); }
    GfxPipeline(GfxPipeline&& o) noexcept : id(o.id) { o.id = SG_INVALID_ID; }
    GfxPipeline& operator=(GfxPipeline&& o) noexcept {
        if (this != &o) {
            if (id != SG_INVALID_ID) sg_destroy_pipeline({id});
            id = o.id;
            o.id = SG_INVALID_ID;
        }
        return *this;
    }
    GfxPipeline(const GfxPipeline&) = delete;
    GfxPipeline& operator=(const GfxPipeline&) = delete;
    operator sg_pipeline() const { return {id}; }
};

struct GfxShader {
    uint32_t id = SG_INVALID_ID;
    GfxShader() = default;
    GfxShader(sg_shader h) : id(h.id) {}
    ~GfxShader() { if (id != SG_INVALID_ID) sg_destroy_shader({id}); }
    GfxShader(GfxShader&& o) noexcept : id(o.id) { o.id = SG_INVALID_ID; }
    GfxShader& operator=(GfxShader&& o) noexcept {
        if (this != &o) {
            if (id != SG_INVALID_ID) sg_destroy_shader({id});
            id = o.id;
            o.id = SG_INVALID_ID;
        }
        return *this;
    }
    GfxShader(const GfxShader&) = delete;
    GfxShader& operator=(const GfxShader&) = delete;
    operator sg_shader() const { return {id}; }
};

struct GfxBuffer {
    uint32_t id = SG_INVALID_ID;
    GfxBuffer() = default;
    GfxBuffer(sg_buffer h) : id(h.id) {}
    ~GfxBuffer() { if (id != SG_INVALID_ID) sg_destroy_buffer({id}); }
    GfxBuffer(GfxBuffer&& o) noexcept : id(o.id) { o.id = SG_INVALID_ID; }
    GfxBuffer& operator=(GfxBuffer&& o) noexcept {
        if (this != &o) {
            if (id != SG_INVALID_ID) sg_destroy_buffer({id});
            id = o.id;
            o.id = SG_INVALID_ID;
        }
        return *this;
    }
    GfxBuffer(const GfxBuffer&) = delete;
    GfxBuffer& operator=(const GfxBuffer&) = delete;
    operator sg_buffer() const { return {id}; }
};

struct GfxSampler {
    uint32_t id = SG_INVALID_ID;
    GfxSampler() = default;
    GfxSampler(sg_sampler h) : id(h.id) {}
    ~GfxSampler() { if (id != SG_INVALID_ID) sg_destroy_sampler({id}); }
    GfxSampler(GfxSampler&& o) noexcept : id(o.id) { o.id = SG_INVALID_ID; }
    GfxSampler& operator=(GfxSampler&& o) noexcept {
        if (this != &o) {
            if (id != SG_INVALID_ID) sg_destroy_sampler({id});
            id = o.id;
            o.id = SG_INVALID_ID;
        }
        return *this;
    }
    GfxSampler(const GfxSampler&) = delete;
    GfxSampler& operator=(const GfxSampler&) = delete;
    operator sg_sampler() const { return {id}; }
};

#endif  // GFX_RESOURCE_H
