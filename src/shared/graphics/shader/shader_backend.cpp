#include "shader_backend.h"

#include <iterator>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "shader_backend_internal.h"
#include "shader_preparation_key.h"

using namespace shader_backend_internal;

namespace {
struct ShaderSpirv {
    std::vector<uint32_t> vertex;
    std::vector<uint32_t> fragment;
};

struct PreparedEntry {
    ShaderSpirv spirv;
    size_t bytes = 0;
    std::list<std::string>::iterator lru;
};

class PreparedCache {
   public:
    bool get(const std::string& key, ShaderSpirv& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = entries_.find(key);
        if (found == entries_.end()) return false;
        lru_.splice(lru_.begin(), lru_, found->second.lru);
        out = found->second.spirv;
        return true;
    }

    void put(std::string key, const ShaderSpirv& spirv) {
        const size_t bytes = key.size() * 2 + (spirv.vertex.size() + spirv.fragment.size()) * sizeof(uint32_t);
        if (bytes > kMaxBytes) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (entries_.find(key) != entries_.end()) return;
        while (!lru_.empty() && used_bytes_ + bytes > kMaxBytes) {
            auto last = std::prev(lru_.end());
            auto found = entries_.find(*last);
            if (found != entries_.end()) {
                used_bytes_ -= found->second.bytes;
                entries_.erase(found);
            }
            lru_.erase(last);
        }
        lru_.push_front(key);
        PreparedEntry entry;
        entry.spirv = spirv;
        entry.bytes = bytes;
        entry.lru = lru_.begin();
        entries_.emplace(std::move(key), std::move(entry));
        used_bytes_ += bytes;
    }

   private:
    static constexpr size_t kMaxBytes = 32u << 20;
    std::mutex mutex_;
    std::unordered_map<std::string, PreparedEntry> entries_;
    std::list<std::string> lru_;
    size_t used_bytes_ = 0;
};

PreparedCache& preparedCache() {
    static PreparedCache cache;
    return cache;
}

bool compileStage(SlangStage stage, const std::string& source, const std::string& name, const char* stage_str,
                  std::vector<uint32_t>& out) {
    const uint64_t hash = ShaderDiskCache::computeHash(stage, source);
    if (shader_cache().tryGet(hash, stage_str, out)) return true;
    if (!compile_spirv_impl(stage, source, name.c_str(), out)) return false;
    shader_cache().put(hash, stage_str, out);
    return true;
}

bool buildSpirv(sg_shader_desc* desc, const std::string& vertex_source, const std::string& fragment_source,
                const char* label, ShaderSpirv& out) {
    if (!desc) return false;
    if (label) desc->label = label;
    if (!prepare_vulkan_bindings(desc)) return false;

    const std::string key = shader_preparation::preparedKey(*desc, vertex_source, fragment_source);
    if (preparedCache().get(key, out)) return true;

    std::string linked_vertex_source = vertex_source;
    std::string linked_fragment_source = fragment_source;
    assign_matching_varying_locations(linked_vertex_source, linked_fragment_source);

    const std::string vulkan_vs = make_vulkan_source(*desc, linked_vertex_source, SG_SHADERSTAGE_VERTEX);
    const std::string vulkan_fs = make_vulkan_source(*desc, linked_fragment_source, SG_SHADERSTAGE_FRAGMENT);
    const std::string vertex_name = std::string(label ? label : "shader") + ".vert";
    const std::string fragment_name = std::string(label ? label : "shader") + ".frag";

    const bool vertex_ok = compileStage(SLANG_STAGE_VERTEX, vulkan_vs, vertex_name, "vert", out.vertex);
    const bool fragment_ok = compileStage(SLANG_STAGE_FRAGMENT, vulkan_fs, fragment_name, "frag", out.fragment);
    if (!vertex_ok || !fragment_ok) return false;
    preparedCache().put(key, out);
    return true;
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

sg_shader create_backend_shader_hlsl(sg_shader_desc* desc, const std::string& vertex_source,
                                     const std::string& fragment_source, const char* label) {
    if (!desc) return {SG_INVALID_ID};
    if (label) desc->label = label;
    const std::string name = label ? label : "shader";

    std::vector<uint32_t> vertex;
    std::vector<uint32_t> fragment;
    if (!get_or_compile_spirv_hlsl(SLANG_STAGE_VERTEX, vertex_source, (name + ".vert").c_str(), "vert", vertex))
        return {SG_INVALID_ID};
    if (!get_or_compile_spirv_hlsl(SLANG_STAGE_FRAGMENT, fragment_source, (name + ".frag").c_str(), "frag", fragment))
        return {SG_INVALID_ID};

    desc->vertex_func.source = nullptr;
    desc->fragment_func.source = nullptr;
    desc->vertex_func.bytecode = {vertex.data(), vertex.size() * sizeof(uint32_t)};
    desc->fragment_func.bytecode = {fragment.data(), fragment.size() * sizeof(uint32_t)};
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
