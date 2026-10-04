#include "disk_cache.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace disk_cache {

const std::string& directory() {
    static const std::string dir = [] {
        const char* xdg = getenv("XDG_CACHE_HOME");
        const char* home = getenv("HOME");
        const std::string base = (xdg && xdg[0]) ? xdg : (home && home[0]) ? std::string(home) + "/.cache" : "/tmp";
        return base + "/linux-wallpaperengine/shaders-v1";
    }();
    return dir;
}

uint64_t hash(const void* data, size_t size, uint64_t seed) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t value = seed;
    for (size_t i = 0; i < size; ++i) {
        value ^= bytes[i];
        value *= 1099511628211ULL;
    }
    return value;
}

uint64_t buildIdentity() {
    static const uint64_t identity = [] {
        struct stat info = {};
        if (stat("/proc/self/exe", &info) != 0) return (uint64_t)0;
        const int64_t parts[] = {(int64_t)info.st_size, (int64_t)info.st_mtim.tv_sec, (int64_t)info.st_mtim.tv_nsec};
        return hash(parts, sizeof(parts));
    }();
    return identity;
}

std::string fileName(const char* prefix, uint64_t key, const char* extension) {
    char name[64];
    snprintf(name, sizeof(name), "%s_%016llx.%s", prefix, (unsigned long long)key, extension);
    return name;
}

bool read(const std::string& name, std::vector<uint8_t>& out) {
    std::ifstream file(directory() + "/" + name, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    const std::streamsize size = file.tellg();
    if (size <= 0) return false;
    file.seekg(0, std::ios::beg);
    out.resize((size_t)size);
    return (bool)file.read(reinterpret_cast<char*>(out.data()), size);
}

void write(const std::string& name, const void* data, size_t size) {
    if (size == 0) return;
    std::error_code ec;
    std::filesystem::create_directories(directory(), ec);

    const std::string path = directory() + "/" + name;
    const std::string temp = path + "." + std::to_string(getpid()) + ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return;
        file.write(static_cast<const char*>(data), (std::streamsize)size);
        if (!file) {
            std::filesystem::remove(temp, ec);
            return;
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) std::filesystem::remove(temp, ec);
}

}  // namespace disk_cache
