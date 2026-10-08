#define STB_TRUETYPE_IMPLEMENTATION
#include "font_repository.h"

#include <stdio.h>

#include "shared/core/vfs.h"

namespace {

bool initFont(LoadedFont& font) {
    if (font.data.empty()) return false;
    const int offset = stbtt_GetFontOffsetForIndex(font.data.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&font.info, font.data.data(), offset)) return false;
    font.ready = true;
    return true;
}

std::shared_ptr<const LoadedFont> loadFontFile(const std::string& path) {
    auto font = std::make_shared<LoadedFont>();
    if (!vfs::readAll(path.c_str(), font->data) || !initFont(*font)) return nullptr;
    return font;
}

// System fonts live outside the package, so they are read straight from disk.
bool readSystemFile(const std::string& path, std::vector<uint8_t>& data) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return false;
    uint8_t chunk[65536];
    size_t count = 0;
    while ((count = fread(chunk, 1, sizeof(chunk), file)) > 0) data.insert(data.end(), chunk, chunk + count);
    fclose(file);
    return !data.empty();
}

std::shared_ptr<const LoadedFont> loadSystemFont(const std::string& path) {
    auto font = std::make_shared<LoadedFont>();
    if (!readSystemFile(path, font->data) || !initFont(*font)) return nullptr;
    return font;
}

std::string matchSystemFont(int codepoint) {
    char command[96];
    snprintf(command, sizeof(command), "fc-match -f '%%{file}' ':charset=%x' 2>/dev/null", (unsigned)codepoint);
    std::string path;
    if (FILE* pipe = popen(command, "r")) {
        char buffer[1024] = {};
        if (fgets(buffer, sizeof(buffer), pipe)) path = buffer;
        pclose(pipe);
    }
    return path;
}

}  // namespace

std::shared_ptr<const LoadedFont> FontRepository::font(const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = files_.find(path);
        if (found != files_.end()) return found->second;
    }
    std::shared_ptr<const LoadedFont> loaded = loadFontFile(path);
    if (!loaded) return nullptr;
    return publish(files_, path, std::move(loaded));
}

std::shared_ptr<const LoadedFont> FontRepository::fallback(int codepoint) {
    const std::string path = systemFontPath(codepoint);
    if (path.empty()) return nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = system_fonts_.find(path);
        if (found != system_fonts_.end()) return found->second;
    }
    std::shared_ptr<const LoadedFont> loaded = loadSystemFont(path);
    if (!loaded) return nullptr;
    return publish(system_fonts_, path, std::move(loaded));
}

// Fontconfig is queried once per 256-codepoint block, so the lookup cost is paid per block, not per glyph.
std::string FontRepository::systemFontPath(int codepoint) {
    const int block = codepoint >> 8;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = block_paths_.find(block);
        if (found != block_paths_.end()) return found->second;
    }
    const std::string path = matchSystemFont(codepoint);
    std::lock_guard<std::mutex> lock(mutex_);
    return block_paths_.try_emplace(block, path).first->second;
}

// Keeps the entry another thread published first, so every caller shares one font.
std::shared_ptr<const LoadedFont> FontRepository::publish(FontMap& map, const std::string& key,
                                                          std::shared_ptr<const LoadedFont> loaded) {
    std::lock_guard<std::mutex> lock(mutex_);
    return map.try_emplace(key, std::move(loaded)).first->second;
}

FontRepository& fontRepository() {
    static FontRepository repository;
    return repository;
}
