#include "shader_backend.h"

#include <string>
#include <vector>

#include "shader_backend_internal.h"

using namespace shader_backend_internal;

namespace {
struct ShaderSpirv {
    std::vector<uint32_t> vertex;
    std::vector<uint32_t> fragment;
};

bool compileStage(SlangStage stage, const std::string& source, const std::string& name, const char* stage_str,
                  std::vector<uint32_t>& out) {
    const uint64_t hash = ShaderDiskCache::computeHash(stage, source);
    if (shader_cache().tryGet(hash, stage_str, out)) return true;
    if (!compile_spirv_impl(stage, source, name.c_str(), out)) return false;
    shader_cache().put(hash, stage_str, out);
    return true;
}

// Everything create_backend_shader does before it touches the GPU, so it can also run on a worker thread.
bool buildSpirv(sg_shader_desc* desc, const std::string& vertex_source, const std::string& fragment_source,
                const char* label, ShaderSpirv& out) {
    if (!desc) return false;
    if (label) desc->label = label;
    if (!prepare_vulkan_bindings(desc)) return false;

    std::string linked_vertex_source = vertex_source;
    std::string linked_fragment_source = fragment_source;
    assign_matching_varying_locations(linked_vertex_source, linked_fragment_source);

    const std::string vulkan_vs = make_vulkan_source(*desc, linked_vertex_source, SG_SHADERSTAGE_VERTEX);
    const std::string vulkan_fs = make_vulkan_source(*desc, linked_fragment_source, SG_SHADERSTAGE_FRAGMENT);
    const std::string vertex_name = std::string(label ? label : "shader") + ".vert";
    const std::string fragment_name = std::string(label ? label : "shader") + ".frag";

    const bool vertex_ok = compileStage(SLANG_STAGE_VERTEX, vulkan_vs, vertex_name, "vert", out.vertex);
    const bool fragment_ok = compileStage(SLANG_STAGE_FRAGMENT, vulkan_fs, fragment_name, "frag", out.fragment);
    return vertex_ok && fragment_ok;
}
}  // namespace

sg_shader create_backend_shader(sg_shader_desc* desc, const std::string& vertex_source,
                                const std::string& fragment_source, const char* label) {
    ShaderSpirv spirv;
    if (!buildSpirv(desc, vertex_source, fragment_source, label, spirv)) return {SG_INVALID_ID};

    desc->vertex_func.source = nullptr;
    desc->fragment_func.source = nullptr;
    desc->vertex_func.bytecode = {spirv.vertex.data(), spirv.vertex.size() * sizeof(uint32_t)};
    desc->fragment_func.bytecode = {spirv.fragment.data(), spirv.fragment.size() * sizeof(uint32_t)};
    desc->vertex_func.entry = "main";
    desc->fragment_func.entry = "main";

    return sg_make_shader(desc);
}

bool prewarm_backend_shader(sg_shader_desc* desc, const std::string& vertex_source, const std::string& fragment_source,
                            const char* label) {
    ShaderSpirv spirv;
    return buildSpirv(desc, vertex_source, fragment_source, label, spirv);
}

void get_shader_cache_stats(uint64_t* out_hits, uint64_t* out_misses) {
    if (out_hits) *out_hits = shader_cache().cache_hits.load(std::memory_order_relaxed);
    if (out_misses) *out_misses = shader_cache().cache_misses.load(std::memory_order_relaxed);
}
