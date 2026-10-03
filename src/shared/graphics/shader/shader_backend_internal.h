#ifndef SHADER_BACKEND_INTERNAL_H
#define SHADER_BACKEND_INTERNAL_H

#include <slang.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "sokol_gfx.h"

struct ShaderDiskCache {
    std::mutex mem_cache_mutex;
    std::unordered_map<uint64_t, std::vector<uint32_t>> in_memory_cache;
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};

    static constexpr const char* kCacheDir = "/tmp/linux-wallpaperengine/shaders";

    static uint64_t computeHash(SlangStage stage, const std::string& source) {
        uint64_t hash = 14695981039346656037ULL;
        hash ^= static_cast<uint64_t>(stage);
        hash *= 1099511628211ULL;
        for (char c : source) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 1099511628211ULL;
        }
        return hash;
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

        char path[512];
        snprintf(path, sizeof(path), "%s/%s_%016llx.spv", kCacheDir, stage_str, (unsigned long long)hash);
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            cache_misses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        const std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        if (size <= 0 || (size % sizeof(uint32_t)) != 0) {
            cache_misses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        out_spirv.resize(size / sizeof(uint32_t));
        if (!file.read(reinterpret_cast<char*>(out_spirv.data()), size)) {
            cache_misses.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mem_cache_mutex);
            in_memory_cache[hash] = out_spirv;
        }

        cache_hits.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void put(uint64_t hash, const char* stage_str, const std::vector<uint32_t>& spirv) {
        if (spirv.empty()) return;

        {
            std::lock_guard<std::mutex> lock(mem_cache_mutex);
            in_memory_cache[hash] = spirv;
        }

        std::error_code ec;
        std::filesystem::create_directories(kCacheDir, ec);

        char path[512];
        snprintf(path, sizeof(path), "%s/%s_%016llx.spv", kCacheDir, stage_str, (unsigned long long)hash);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(uint32_t));
        }
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
}  // namespace shader_backend_internal

#endif  // SHADER_BACKEND_INTERNAL_H
