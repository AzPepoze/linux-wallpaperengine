#ifndef SHADER_BACKEND_INTERNAL_H
#define SHADER_BACKEND_INTERNAL_H

#include <slang.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
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

    // Bound on the RAM copy; the disk cache backs everything beyond it.
    static constexpr size_t kMaxMemoryCacheBytes = 8u << 20;
    size_t memory_cache_bytes = 0;

    // Bump the suffix whenever a compiler or rewrite change invalidates old binaries.
    static const std::string& cacheDir() {
        static const std::string dir = [] {
            const char* xdg = getenv("XDG_CACHE_HOME");
            const char* home = getenv("HOME");
            std::string base = (xdg && xdg[0]) ? xdg : (home && home[0]) ? std::string(home) + "/.cache" : "/tmp";
            return base + "/linux-wallpaperengine/shaders-v1";
        }();
        return dir;
    }

    static std::string cachePath(uint64_t hash, const char* stage_str) {
        char name[48];
        snprintf(name, sizeof(name), "/%s_%016llx.spv", stage_str, (unsigned long long)hash);
        return cacheDir() + name;
    }

    void remember(uint64_t hash, const std::vector<uint32_t>& spirv) {
        std::lock_guard<std::mutex> lock(mem_cache_mutex);
        const size_t bytes = spirv.size() * sizeof(uint32_t);
        if (memory_cache_bytes + bytes > kMaxMemoryCacheBytes) return;
        if (in_memory_cache.emplace(hash, spirv).second) memory_cache_bytes += bytes;
    }

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

        std::ifstream file(cachePath(hash, stage_str), std::ios::binary | std::ios::ate);
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

        remember(hash, out_spirv);
        cache_hits.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void put(uint64_t hash, const char* stage_str, const std::vector<uint32_t>& spirv) {
        if (spirv.empty()) return;

        remember(hash, spirv);

        std::error_code ec;
        std::filesystem::create_directories(cacheDir(), ec);

        // Write-then-rename so a crash or a concurrent run never leaves a truncated binary behind.
        const std::string path = cachePath(hash, stage_str);
        const std::string temp = path + "." + std::to_string(getpid()) + ".tmp";
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) return;
            file.write(reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(uint32_t));
            if (!file) {
                std::filesystem::remove(temp, ec);
                return;
            }
        }
        std::filesystem::rename(temp, path, ec);
        if (ec) std::filesystem::remove(temp, ec);
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
