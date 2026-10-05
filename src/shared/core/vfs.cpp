#include "vfs.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "logger.h"

namespace {
struct Entry {
    size_t offset;
    size_t size;
};

struct Package {
    std::string source_directory;
    const uint8_t* base = nullptr;
    size_t size = 0;
    std::unordered_map<std::string, Entry> files;
};

Package g_package;

const char kPrefix[] = "pkg:/";
constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;

bool readU32(size_t& pos, uint32_t& out) {
    if (pos + 4 > g_package.size) return false;
    memcpy(&out, g_package.base + pos, 4);
    pos += 4;
    return true;
}

bool parseIndex() {
    size_t pos = 0;
    if (g_package.size < 12) return false;
    if (memcmp(g_package.base, "PKGV", 4) != 0) {
        // Some packages have 4 bytes of unknown data before PKGV.
        pos = 4;
        if (memcmp(g_package.base + pos, "PKGV", 4) != 0) return false;
    }
    pos += 8;

    uint32_t count = 0;
    if (!readU32(pos, count)) return false;

    struct Raw {
        std::string name;
        uint32_t offset;
        uint32_t size;
    };
    std::vector<Raw> raw;
    raw.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t name_len = 0;
        if (!readU32(pos, name_len) || pos + name_len > g_package.size) return false;
        Raw entry;
        entry.name.assign(reinterpret_cast<const char*>(g_package.base + pos), name_len);
        pos += name_len;
        if (!readU32(pos, entry.offset) || !readU32(pos, entry.size)) return false;
        raw.push_back(std::move(entry));
    }

    // Entry offsets are relative to the end of the index.
    g_package.files.reserve(raw.size());
    for (Raw& entry : raw) {
        const size_t start = pos + entry.offset;
        if (start + entry.size > g_package.size) {
            LOG_W("vfs: %s lies outside the package, skipping", entry.name.c_str());
            continue;
        }
        g_package.files[std::move(entry.name)] = {start, entry.size};
    }
    return true;
}

const Entry* lookup(const char* path) {
    if (!path || strncmp(path, kPrefix, kPrefixLen) != 0) return nullptr;
    const auto it = g_package.files.find(path + kPrefixLen);
    return it == g_package.files.end() ? nullptr : &it->second;
}
}  // namespace

namespace vfs {

bool mount(const char* pkg_path) {
    unmount();
    const int fd = ::open(pkg_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st = {};
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return false;
    }
    void* map = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return false;

    g_package.base = static_cast<const uint8_t*>(map);
    g_package.size = (size_t)st.st_size;
    if (!parseIndex()) {
        unmount();
        return false;
    }
    g_package.source_directory = std::filesystem::absolute(pkg_path).parent_path().string();
    LOG_I("vfs: mapped %s (%zu files, %zu MB)", pkg_path, g_package.files.size(), g_package.size >> 20);
    return true;
}

void unmount() {
    if (g_package.base) munmap(const_cast<uint8_t*>(g_package.base), g_package.size);
    g_package = Package();
}

bool mounted() {
    return g_package.base != nullptr;
}

std::string sourceDirectory() {
    return g_package.source_directory;
}

bool isVirtual(const char* path) {
    return path && strncmp(path, kPrefix, kPrefixLen) == 0;
}

bool exists(const char* path) {
    if (isVirtual(path)) return lookup(path) != nullptr;
    return path && access(path, F_OK) == 0;
}

bool find(const char* path, const uint8_t*& data, size_t& size) {
    const Entry* entry = lookup(path);
    if (!entry) return false;
    data = g_package.base + entry->offset;
    size = entry->size;
    return true;
}

bool readAll(const char* path, std::vector<uint8_t>& out) {
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (find(path, data, size)) {
        out.assign(data, data + size);
        return true;
    }
    std::FILE* file = isVirtual(path) ? nullptr : std::fopen(path, "rb");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END);
    const long length = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    out.resize(length > 0 ? (size_t)length : 0);
    const size_t n = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), file);
    std::fclose(file);
    return n == out.size();
}

std::FILE* open(const char* path) {
    if (!isVirtual(path)) return std::fopen(path, "rb");
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (!find(path, data, size)) return nullptr;
    // fmemopen rejects empty buffers.
    if (size == 0) return std::fopen("/dev/null", "rb");
    return fmemopen(const_cast<uint8_t*>(data), size, "rb");
}

void forEachFile(const std::function<bool(const char*)>& visitor) {
    for (const auto& file : g_package.files) {
        if (visitor(file.first.c_str())) return;
    }
}

}  // namespace vfs
