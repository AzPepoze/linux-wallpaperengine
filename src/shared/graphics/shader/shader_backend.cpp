#include "shader_backend.h"

#include <future>
#include <string>
#include <vector>

#include "shader_backend_internal.h"

using namespace shader_backend_internal;

sg_shader create_backend_shader(sg_shader_desc* desc, const std::string& vertex_source,
                                const std::string& fragment_source, const char* label) {
    if (!desc) return {SG_INVALID_ID};
    if (label) desc->label = label;

    if (!prepare_vulkan_bindings(desc)) return {SG_INVALID_ID};

    std::string linked_vertex_source = vertex_source;
    std::string linked_fragment_source = fragment_source;
    assign_matching_varying_locations(linked_vertex_source, linked_fragment_source);

    const std::string vulkan_vs = make_vulkan_source(*desc, linked_vertex_source, SG_SHADERSTAGE_VERTEX);
    const std::string vulkan_fs = make_vulkan_source(*desc, linked_fragment_source, SG_SHADERSTAGE_FRAGMENT);

    std::vector<uint32_t> vertex_spirv;
    std::vector<uint32_t> fragment_spirv;
    const std::string vertex_name = std::string(label ? label : "shader") + ".vert";
    const std::string fragment_name = std::string(label ? label : "shader") + ".frag";

    const uint64_t vert_hash = ShaderDiskCache::computeHash(SLANG_STAGE_VERTEX, vulkan_vs);
    const uint64_t frag_hash = ShaderDiskCache::computeHash(SLANG_STAGE_FRAGMENT, vulkan_fs);

    const bool vert_cached = shader_cache().tryGet(vert_hash, "vert", vertex_spirv);
    const bool frag_cached = shader_cache().tryGet(frag_hash, "frag", fragment_spirv);

    bool vertex_ok = vert_cached;
    bool fragment_ok = frag_cached;

    if (!vert_cached && !frag_cached) {
        auto vert_future = std::async(std::launch::async, [&]() {
            if (compile_spirv_impl(SLANG_STAGE_VERTEX, vulkan_vs, vertex_name.c_str(), vertex_spirv)) {
                shader_cache().put(vert_hash, "vert", vertex_spirv);
                return true;
            }
            return false;
        });

        auto frag_future = std::async(std::launch::async, [&]() {
            if (compile_spirv_impl(SLANG_STAGE_FRAGMENT, vulkan_fs, fragment_name.c_str(), fragment_spirv)) {
                shader_cache().put(frag_hash, "frag", fragment_spirv);
                return true;
            }
            return false;
        });

        vertex_ok = vert_future.get();
        fragment_ok = frag_future.get();
    } else {
        if (!vert_cached) {
            vertex_ok = get_or_compile_spirv(SLANG_STAGE_VERTEX, vulkan_vs, vertex_name.c_str(), "vert", vertex_spirv);
        }
        if (!frag_cached) {
            fragment_ok =
                get_or_compile_spirv(SLANG_STAGE_FRAGMENT, vulkan_fs, fragment_name.c_str(), "frag", fragment_spirv);
        }
    }

    if (!vertex_ok || !fragment_ok) return {SG_INVALID_ID};

    desc->vertex_func.source = nullptr;
    desc->fragment_func.source = nullptr;
    desc->vertex_func.bytecode = {vertex_spirv.data(), vertex_spirv.size() * sizeof(uint32_t)};
    desc->fragment_func.bytecode = {fragment_spirv.data(), fragment_spirv.size() * sizeof(uint32_t)};
    desc->vertex_func.entry = "main";
    desc->fragment_func.entry = "main";

    return sg_make_shader(desc);
}

void get_shader_cache_stats(uint64_t* out_hits, uint64_t* out_misses) {
    if (out_hits) *out_hits = shader_cache().cache_hits.load(std::memory_order_relaxed);
    if (out_misses) *out_misses = shader_cache().cache_misses.load(std::memory_order_relaxed);
}
