#ifndef VFS_H
#define VFS_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Read-only view of a .pkg in memory; other paths fall through to the real filesystem.
namespace vfs {

struct Package;
using PackageHandle = std::shared_ptr<const Package>;

// Mounts a package as an immutable mapping; each worker binds it to resolve pkg:/ paths.
PackageHandle loadPackage(const char* pkg_path);
PackageHandle currentPackage();
void bindPackage(PackageHandle package);

class ScopedBinding {
   public:
    explicit ScopedBinding(PackageHandle package);
    ~ScopedBinding();
    ScopedBinding(const ScopedBinding&) = delete;
    ScopedBinding& operator=(const ScopedBinding&) = delete;

   private:
    PackageHandle previous_;
};

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
