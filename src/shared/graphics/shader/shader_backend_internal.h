#ifndef SHADER_BACKEND_INTERNAL_H
#define SHADER_BACKEND_INTERNAL_H

#include <slang.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "shared/core/disk_cache.h"
#include "sokol_gfx.h"

struct ShaderDiskCache {
    std::mutex mem_cache_mutex;
    std::unordered_map<uint64_t, std::vector<uint32_t>> in_memory_cache;
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};

    static constexpr size_t kMaxMemoryCacheBytes = 8u << 20;
    size_t memory_cache_bytes = 0;

    static uint64_t computeHash(SlangStage stage, const std::string& source) {
        const uint64_t seed = disk_cache::hash(&stage, sizeof(stage));
        return disk_cache::hash(source.data(), source.size(), seed);
    }

    void remember(uint64_t hash, const std::vector<uint32_t>& spirv) {
        std::lock_guard<std::mutex> lock(mem_cache_mutex);
        const size_t bytes = spirv.size() * sizeof(uint32_t);
        if (memory_cache_bytes + bytes > kMaxMemoryCacheBytes) return;
        if (in_memory_cache.emplace(hash, spirv).second) memory_cache_bytes += bytes;
    }

    bool tryGet(uint64_t hash, const char* stage_str, std::vector<uint32_t>& out_spirv) {
        {
            std::lock_guard<std::mutex> lock(mem_cache_mutex);
            auto it = in_memory_cache.find(hash);
            if (it != in_memory_cache.end()) {
                out_spirv = it->second;
                cache_hits.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }

        std::vector<uint8_t> bytes;
        if (!disk_cache::read(disk_cache::fileName(stage_str, hash, "spv"), bytes) ||
            bytes.size() % sizeof(uint32_t) != 0) {
            cache_misses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        out_spirv.resize(bytes.size() / sizeof(uint32_t));
        memcpy(out_spirv.data(), bytes.data(), bytes.size());

        remember(hash, out_spirv);
        cache_hits.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void put(uint64_t hash, const char* stage_str, const std::vector<uint32_t>& spirv) {
        if (spirv.empty()) return;
        remember(hash, spirv);
        disk_cache::write(disk_cache::fileName(stage_str, hash, "spv"), spirv.data(), spirv.size() * sizeof(uint32_t));
    }
};

inline ShaderDiskCache& shader_cache() {
    static ShaderDiskCache cache;
    return cache;
}

namespace shader_backend_internal {
bool prepare_vulkan_bindings(sg_shader_desc* desc);
std::string make_vulkan_source(const sg_shader_desc& desc, const std::string& original, sg_shader_stage stage);
void assign_matching_varying_locations(std::string& vertex_source, std::string& fragment_source);

bool compile_spirv_impl(SlangStage stage, const std::string& source, const char* source_name,
                        std::vector<uint32_t>& output);
bool get_or_compile_spirv(SlangStage stage, const std::string& source, const char* source_name, const char* stage_str,
                          std::vector<uint32_t>& output);
// Compiles raw HLSL (Wallpaper Engine's DX11 fallback shaders) to SPIR-V.
bool get_or_compile_spirv_hlsl(SlangStage stage, const std::string& source, const char* source_name,
                               const char* stage_str, std::vector<uint32_t>& output);
}  // namespace shader_backend_internal

#endif  // SHADER_BACKEND_INTERNAL_H
