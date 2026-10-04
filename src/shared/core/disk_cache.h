#ifndef DISK_CACHE_H
#define DISK_CACHE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Content-addressed files under $XDG_CACHE_HOME/linux-wallpaperengine (default ~/.cache/...). Writes are atomic,
// so a crash or a second running instance never leaves a truncated entry behind.
namespace disk_cache {

// Bump the suffix when the on-disk layout changes.
const std::string& directory();

uint64_t hash(const void* data, size_t size, uint64_t seed = 14695981039346656037ULL);

// Identifies this build of the executable, for entries that depend on code rather than only on their input.
uint64_t buildIdentity();

std::string fileName(const char* prefix, uint64_t key, const char* extension);

bool read(const std::string& name, std::vector<uint8_t>& out);
void write(const std::string& name, const void* data, size_t size);

}  // namespace disk_cache

#endif  // DISK_CACHE_H
