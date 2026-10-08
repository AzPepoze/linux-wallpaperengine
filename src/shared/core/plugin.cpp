#include "shared/core/plugin.h"

#include <dlfcn.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "shared/core/logger.h"

namespace {
// Each name is loaded at most once; a failed load stores null so it is not retried or logged again.
std::map<std::string, void*>& loadedPlugins() {
    static std::map<std::string, void*> plugins;
    return plugins;
}

std::filesystem::path executableDirectory() {
    std::error_code error;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path() : exe.parent_path();
}

// Where to look for lib<name>.so, most specific first.
std::filesystem::path findPlugin(const std::string& file) {
    std::vector<std::filesystem::path> directories;
    if (const char* override_dir = std::getenv("LWE_PLUGIN_DIR")) directories.emplace_back(override_dir);
    const std::filesystem::path exe_dir = executableDirectory();
    directories.push_back(exe_dir);
    directories.push_back(exe_dir / ".." / "lib");
    directories.push_back(exe_dir / ".." / "lib" / "linux-wallpaperengine");
    for (const auto& directory : directories) {
        std::error_code error;
        const std::filesystem::path candidate = directory / file;
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
    }
    return {};
}
}  // namespace

void* loadPlugin(const char* name) {
    auto& plugins = loadedPlugins();
    const auto found = plugins.find(name);
    if (found != plugins.end()) return found->second;

    const std::string file = std::string("lib") + name + ".so";
    void*& handle = plugins[name];
    const std::filesystem::path path = findPlugin(file);
    if (path.empty()) {
        LOG_W("Plugin %s not found; the feature it provides is disabled", file.c_str());
        return nullptr;
    }

    handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        LOG_W("Plugin %s failed to load: %s", path.c_str(), dlerror());
        return nullptr;
    }

    using AbiFunction = int (*)();
    const auto abi = reinterpret_cast<AbiFunction>(dlsym(handle, "lwe_plugin_abi"));
    if (!abi || abi() != kPluginAbi) {
        LOG_W("Plugin %s was built for another engine version; it is not used", path.c_str());
        dlclose(handle);
        handle = nullptr;
        return nullptr;
    }

    LOG_I("Loaded plugin %s", path.c_str());
    return handle;
}

void* pluginSymbol(const char* name, const char* symbol) {
    void* handle = loadPlugin(name);
    return handle ? dlsym(handle, symbol) : nullptr;
}
