#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "shared/core/vfs.h"
#include "test_util.h"

namespace {
struct File {
    std::string name;
    std::string contents;
};

void writeU32(std::ofstream& out, uint32_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

std::filesystem::path writePackage(const std::filesystem::path& dir, const char* filename, const std::string& shared,
                                   const char* unique_name, const std::string& unique) {
    const std::vector<File> files = {{"shared.txt", shared}, {unique_name, unique}};
    const auto path = dir / filename;
    std::ofstream out(path, std::ios::binary);
    out.write("PKGV0001", 8);
    writeU32(out, static_cast<uint32_t>(files.size()));
    uint32_t offset = 0;
    for (const auto& file : files) {
        writeU32(out, static_cast<uint32_t>(file.name.size()));
        out.write(file.name.data(), static_cast<std::streamsize>(file.name.size()));
        writeU32(out, offset);
        writeU32(out, static_cast<uint32_t>(file.contents.size()));
        offset += static_cast<uint32_t>(file.contents.size());
    }
    for (const auto& file : files) out.write(file.contents.data(), static_cast<std::streamsize>(file.contents.size()));
    return path;
}

bool hasContents(const char* path, const std::string& expected) {
    std::vector<uint8_t> bytes;
    if (!vfs::readAll(path, bytes)) return false;
    return std::string(bytes.begin(), bytes.end()) == expected;
}

void testRetainedMappingAndScopedWorkerBindings() {
    char pattern[] = "/tmp/lwe-vfs-package-XXXXXX";
    const char* created = mkdtemp(pattern);
    test::expect("vfs package", created != nullptr, "temporary directory created");
    if (!created) return;
    const std::filesystem::path dir(created);
    const auto path_a = writePackage(dir, "a.pkg", "from-a", "only-a.txt", "a-only");
    const auto path_b = writePackage(dir, "b.pkg", "from-b", "only-b.txt", "b-only");

    const auto a = vfs::loadPackage(path_a.c_str());
    const auto b = vfs::loadPackage(path_b.c_str());
    test::expect("vfs package", a && b, "both package mappings load independently");
    if (!a || !b) {
        std::filesystem::remove_all(dir);
        return;
    }

    vfs::bindPackage(a);
    vfs::unmount();
    test::expect("vfs package", !vfs::mounted(), "unmount clears only the current thread binding");
    {
        vfs::ScopedBinding binding(a);
        test::expect("vfs package", hasContents("pkg:/shared.txt", "from-a"),
                     "retained package remains readable after unmount");
    }
    test::expect("vfs package", !vfs::mounted(), "scoped binding restores the previous empty binding");

    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    bool a_ok = true;
    bool b_ok = true;
    auto worker = [&](vfs::PackageHandle package, const char* unique_path, const std::string& shared,
                      const std::string& unique, bool& ok) {
        ++ready;
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        vfs::ScopedBinding binding(std::move(package));
        for (int i = 0; i < 500; ++i) {
            if (!hasContents("pkg:/shared.txt", shared) || !hasContents(unique_path, unique) || !vfs::mounted()) {
                ok = false;
                return;
            }
            std::this_thread::yield();
        }
    };
    std::thread ta(worker, a, "pkg:/only-a.txt", "from-a", "a-only", std::ref(a_ok));
    std::thread tb(worker, b, "pkg:/only-b.txt", "from-b", "b-only", std::ref(b_ok));
    while (ready.load(std::memory_order_acquire) != 2) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    ta.join();
    tb.join();

    test::expect("vfs package", a_ok && b_ok,
                 "simultaneous worker bindings resolve overlapping names from their own packages");
    test::expect("vfs package", !vfs::mounted(), "worker bindings do not change the caller thread binding");
    std::filesystem::remove_all(dir);
}
}  // namespace

int main() {
    testRetainedMappingAndScopedWorkerBindings();
    return test::finish("vfs_package_tests");
}
