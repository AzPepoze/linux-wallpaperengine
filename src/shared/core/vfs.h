#ifndef VFS_H
#define VFS_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// Read-only view of a Wallpaper Engine .pkg mapped into memory. Package files are addressed as "pkg:/<name>";
// every other path falls through to the real filesystem, so callers need not care which one they hold.
namespace vfs {

constexpr const char* kRoot = "pkg:";

bool mount(const char* pkg_path);
void unmount();
bool mounted();
// Files beside the mounted package (notably project.json) remain on disk.
std::string sourceDirectory();

bool isVirtual(const char* path);
bool exists(const char* path);
bool find(const char* path, const uint8_t*& data, size_t& size);
bool readAll(const char* path, std::vector<uint8_t>& out);
std::FILE* open(const char* path);

void forEachFile(const std::function<bool(const char*)>& visitor);

}  // namespace vfs

#endif  // VFS_H
