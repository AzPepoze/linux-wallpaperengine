#ifndef FONT_REPOSITORY_H
#define FONT_REPOSITORY_H

#include <stb/stb_truetype.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// A parsed font. Published fonts are immutable: `info` points into `data`, which is never resized after load.
struct LoadedFont {
    std::vector<uint8_t> data;
    stbtt_fontinfo info = {};
    bool ready = false;
};

// Thread-safe font cache. Fonts are read and parsed outside the lock and are never evicted, so the
// stbtt_fontinfo pointers taken from a font stay valid for the life of the process.
class FontRepository {
   public:
    // The authored font file at `path`, or nullptr when it cannot be read or parsed.
    std::shared_ptr<const LoadedFont> font(const std::string& path);
    // The installed system font that covers `codepoint`, or nullptr when none does.
    std::shared_ptr<const LoadedFont> fallback(int codepoint);

   private:
    using FontMap = std::map<std::string, std::shared_ptr<const LoadedFont>>;

    std::string systemFontPath(int codepoint);
    std::shared_ptr<const LoadedFont> publish(FontMap& map, const std::string& key,
                                              std::shared_ptr<const LoadedFont> loaded);

    std::mutex mutex_;
    FontMap files_;
    FontMap system_fonts_;
    std::map<int, std::string> block_paths_;
};

FontRepository& fontRepository();

#endif  // FONT_REPOSITORY_H
