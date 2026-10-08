#include "app/cli_args.h"

#include <string.h>

#include <algorithm>
#include <string>

#include "shared/core/build_config.h"

namespace cli_args {
namespace {

constexpr CliOption kSentinel = {{nullptr}, nullptr};

// Each group array ends with a sentinel row (names[0] == nullptr).
constexpr CliOption kWallpaper[] = {
    {{"<wallpaper>", nullptr}, nullptr, CliBuild::All, "Wallpaper project directory, .pkg file or video file"},
    {{"--pkg", "-pkg", nullptr}, "<path>", CliBuild::All, "Treat the given path as a package"},
    {{"--extract-only", "-extract-only", nullptr}, nullptr, CliBuild::All, "Extract the package and exit"},
    {{"--extract-dir", "-extract-dir", nullptr}, "<path>", CliBuild::All, "Target directory for extraction"},
    {{"--assets-dir", nullptr}, "<path>", CliBuild::All, "Wallpaper Engine install root or its assets/ directory"},
    {{"--set-property", nullptr}, "<name=value>", CliBuild::All, "Override a project property (repeatable)"},
    kSentinel,
};

constexpr CliOption kGraphics[] = {
    {{"--performance-profile", nullptr}, nullptr, CliBuild::All, "Log presented FPS and frame intervals every 10 s"},
    {{"--intro-zoom", nullptr}, "<factor>", CliBuild::All, "Startup zoom factor (default 1.0)"},
    {{"--intro-duration", nullptr}, "<seconds>", CliBuild::All, "Startup zoom duration (default 4)"},
    {{"--effect-resolution", nullptr},
     "<auto|native>",
     CliBuild::All,
     "Verified shake effect resolution (default auto)"},
    {{"--gpu", "-gpu", nullptr}, "<id>", CliBuild::All, "Select a GPU by index or name"},
    {{"--list-gpus", "-list-gpus", nullptr}, nullptr, CliBuild::All, "List available GPUs and exit"},
    {{"-f", "--fps", nullptr}, "<n>", CliBuild::All, "Frame-rate cap; 0 or omitted follows the display"},
    {{"--scaling", nullptr}, "<default|fit|fill|stretch>", CliBuild::All, "fill crops to cover, fit letterboxes"},
    {{"--clamp", nullptr}, "<mode>", CliBuild::All, "Accepted and ignored"},
    {{"--cover", nullptr}, nullptr, CliBuild::All, "Force cover scaling, ignoring the project's fit"},
    {{"--video-ram", nullptr}, nullptr, CliBuild::All, "Load video files fully into RAM instead of streaming"},
    {{"--script-profile", nullptr}, nullptr, CliBuild::All, "Log the most expensive scripts every ~10 s"},
    kSentinel,
};

constexpr CliOption kDisplay[] = {
    {{"-r", "--screen-root", nullptr}, "<output>", CliBuild::All, "Draw as a layer-shell surface on the named output"},
    {{"--layer", nullptr}, "<background|bottom|top|overlay>", CliBuild::All, "Layer-shell layer (default background)"},
    {{"--layer-size", nullptr}, "<WxH>", CliBuild::Debug, "Use a small anchored rectangle (for example 320x180)"},
    {{"--layer-anchor", nullptr}, "<edges>", CliBuild::Debug, "Anchor edges for --layer-size (for example top-left)"},
    kSentinel,
};

constexpr CliOption kTransition[] = {
    {{"--transition", nullptr}, "<name|none|random>", CliBuild::All, "Transition shader (default fade; 0-26 accepted)"},
    {{"--transition-duration", nullptr}, "<ms>", CliBuild::All, "Transition length in milliseconds (default 1000)"},
    {{"--transition-mode", nullptr}, "<freeze|continue>", CliBuild::All, "Outgoing wallpaper behavior during the fade"},
    kSentinel,
};

constexpr CliOption kControl[] = {
    {{"--no-control", nullptr}, nullptr, CliBuild::All, "Do not hand off to or own a control socket"},
    kSentinel,
};

constexpr CliOption kAudio[] = {
    {{"--no-audio", nullptr}, nullptr, CliBuild::All, "Disable audio"},
    {{"-s", "--silent", "--mute", nullptr}, nullptr, CliBuild::All, "Disable audio (alias of --no-audio)"},
    {{"--volume", nullptr}, "<n>", CliBuild::All, "Master volume percent (0-100)"},
    kSentinel,
};

constexpr CliOption kWeb[] = {
    {{"--no-web-devtools", nullptr}, nullptr, CliBuild::All, "Disable the localhost remote-debug server"},
    {{"--web-devtools-port", nullptr}, "<port>", CliBuild::All, "DevTools port (default 9222)"},
    {{"--web-devtools-browser", nullptr}, "<cmd>", CliBuild::All, "Browser command for DevTools (default xdg-open)"},
    {{"--web-transport", nullptr}, "<auto|dma-buf|off-screen|snapshot>", CliBuild::All, "Transport (default auto)"},
    kSentinel,
};

constexpr CliOption kDiagnostics[] = {
    {{"--sandbox", nullptr}, nullptr, CliBuild::Debug, "Run the debug effect sandbox"},
    {{"--no-ui", nullptr}, nullptr, CliBuild::Debug, "Start without the ImGui UI"},
    {{"--diagnose", "--diagnostics", nullptr}, nullptr, CliBuild::Debug, "Enable render diagnostics"},
    {{"--diagnose-frame", nullptr}, "<n>", CliBuild::Debug, "Frame to capture (default 100)"},
    {{"--diagnose-final-only", nullptr}, nullptr, CliBuild::Debug, "Capture only the final output"},
    {{"--diagnose-deterministic", nullptr}, nullptr, CliBuild::Debug, "Fixed 1/60 s step and seeded RNG"},
    {{"--exit-after-diagnose", nullptr}, nullptr, CliBuild::Debug, "Quit once the capture completes"},
    {{"--disable-effects", nullptr}, "<pattern>", CliBuild::Debug, "Disable effects matching substring (*/all = all)"},
    {{"--disable-particles", nullptr}, nullptr, CliBuild::Debug, "Disable particles in diagnostics"},
    {{"--disable-bloom", nullptr}, nullptr, CliBuild::Debug, "Disable bloom in diagnostics"},
    kSentinel,
};

constexpr CliOption kParticles[] = {
    {{"--particle-debug", "--particle-debug-bounds", nullptr}, nullptr, CliBuild::All, "Draw particle bounds"},
    {{"--particle-debug-velocity", nullptr}, nullptr, CliBuild::All, "Draw particle velocity vectors"},
    {{"--particle-debug-velocity-scale", nullptr}, "<f>", CliBuild::All, "Velocity vector scale"},
    {{"--particle-debug-max-particles", nullptr}, "<n>", CliBuild::All, "Cap the number of particles drawn"},
    kSentinel,
};

constexpr CliOption kInfo[] = {
    {{"--whoareyou", nullptr}, nullptr, CliBuild::All, "Print engine identity as JSON and exit"},
    kSentinel,
};

// Launcher flags are consumed so they never become the positional wallpaper.
constexpr CliOption kCompatibility[] = {
    {{"--noautomute", nullptr}, nullptr, CliBuild::All, "Accepted for launcher compatibility; ignored", true},
    {{"--no-audio-processing", nullptr}, nullptr, CliBuild::All, "Accepted for launcher compatibility; ignored", true},
    {{"--disable-mouse", nullptr}, nullptr, CliBuild::All, "Accepted for launcher compatibility; ignored", true},
    {{"--disable-parallax", nullptr}, nullptr, CliBuild::All, "Accepted for launcher compatibility; ignored", true},
    {{"--no-fullscreen-pause", nullptr}, nullptr, CliBuild::All, "Accepted for launcher compatibility; ignored", true},
    {{"--fullscreen-pause-only-active", nullptr},
     nullptr,
     CliBuild::All,
     "Accepted for launcher compatibility; ignored",
     true},
    {{"--fullscreen-pause-ignore-appid", nullptr},
     "<id>",
     CliBuild::All,
     "Accepted for launcher compatibility; ignored",
     true},
    {{"--screenshot", nullptr}, "<path>", CliBuild::All, "Accepted for launcher compatibility; ignored", true},
    {{"--screenshot-delay", nullptr}, "<n>", CliBuild::All, "Accepted for launcher compatibility; ignored", true},
    {{"--screen-span", nullptr}, "<names>", CliBuild::All, "Accepted for launcher compatibility; ignored", true},
    {{"--dump-structure", nullptr}, nullptr, CliBuild::All, "Accepted for launcher compatibility; ignored", true},
    kSentinel,
};

constexpr CliGroupDef kGroups[] = {
    {CliGroup::Wallpaper, "Wallpaper", kWallpaper},
    {CliGroup::Graphics, "Graphics", kGraphics},
    {CliGroup::Display, "Display", kDisplay},
    {CliGroup::Transition, "Transition", kTransition},
    {CliGroup::Control, "Control", kControl},
    {CliGroup::Audio, "Audio", kAudio},
    {CliGroup::Web, "Web", kWeb},
    {CliGroup::Diagnostics, "Diagnostics", kDiagnostics},
    {CliGroup::Particles, "Particles", kParticles},
    {CliGroup::Info, "Info", kInfo},
    {CliGroup::Compatibility, "Compatibility", kCompatibility},
};
static_assert(sizeof(kGroups) / sizeof(kGroups[0]) == static_cast<size_t>(CliGroup::Count),
              "cli group table out of sync with CliGroup");

bool visible(const CliOption& option) {
    return option.build != CliBuild::Debug || DEBUG_BUILD;
}

size_t rowWidth(const CliOption& option) {
    size_t width = strlen(option.names[0]);
    for (int i = 1; option.names[i]; ++i) width += 2 + strlen(option.names[i]);
    if (option.value) width += 1 + strlen(option.value);
    return width;
}

void printRow(FILE* out, const CliOption& option, size_t width) {
    std::string names = option.names[0];
    for (int i = 1; option.names[i]; ++i) names += std::string(", ") + option.names[i];
    if (option.value) names += std::string(" ") + option.value;
    fprintf(out, "  %-*s  %s\n", static_cast<int>(width), names.c_str(), option.description);
}

const CliOption* findOption(const std::string& arg) {
    for (const CliGroupDef& def : kGroups)
        for (const CliOption* option = def.options; option->names[0]; ++option)
            for (int i = 0; option->names[i]; ++i) {
                if (arg == option->names[i]) return option;
                if (arg.rfind(std::string(option->names[i]) + "=", 0) == 0) return option;
            }
    return nullptr;
}

}  // namespace

const CliGroupDef* cliGroups() {
    return kGroups;
}

bool takesValue(const std::string& arg) {
    for (const CliGroupDef& def : kGroups)
        for (const CliOption* option = def.options; option->names[0]; ++option)
            for (int i = 0; option->names[i]; ++i)
                if (arg == option->names[i]) return option->value != nullptr;
    return false;
}

void printHelp(FILE* out) {
    size_t width = 0;
    for (const CliGroupDef& def : kGroups)
        for (const CliOption* option = def.options; option->names[0]; ++option)
            if (visible(*option)) width = std::max(width, rowWidth(*option));

    fprintf(out, "Usage: linux-wallpaperengine [options] <wallpaper>\n");
    for (const CliGroupDef& def : kGroups) {
        bool any_visible = false;
        for (const CliOption* option = def.options; option->names[0]; ++option) {
            if (!visible(*option)) continue;
            any_visible = true;
            break;
        }
        if (!any_visible) continue;
        fprintf(out, "\n%s:\n", def.title);
        for (const CliOption* option = def.options; option->names[0]; ++option)
            if (visible(*option)) printRow(out, *option, width);
    }
}

bool optionValue(const std::vector<std::string>& args, const std::vector<std::string>& names, std::string& out) {
    bool found = false;
    for (size_t i = 1; i < args.size(); ++i) {
        for (const std::string& name : names) {
            if (args[i] == name && i + 1 < args.size()) {
                out = args[i + 1];
                found = true;
            } else if (args[i].rfind(name + "=", 0) == 0) {
                out = args[i].substr(name.size() + 1);
                found = true;
            }
        }
    }
    return found;
}

std::vector<std::string> optionValues(const std::vector<std::string>& args, const std::vector<std::string>& names) {
    std::vector<std::string> values;
    for (size_t i = 1; i < args.size(); ++i) {
        for (const std::string& name : names) {
            if (args[i] == name && i + 1 < args.size()) {
                values.push_back(args[i + 1]);
                ++i;
                break;
            }
            if (args[i].rfind(name + "=", 0) == 0) {
                values.push_back(args[i].substr(name.size() + 1));
                break;
            }
        }
    }
    return values;
}

bool hasFlag(const std::vector<std::string>& args, const std::vector<std::string>& names) {
    for (size_t i = 1; i < args.size(); ++i)
        if (std::find(names.begin(), names.end(), args[i]) != names.end()) return true;
    return false;
}

std::string positional(const std::vector<std::string>& args) {
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg.empty()) continue;
        if (arg[0] == '-') {
            if (takesValue(arg)) ++i;
            continue;
        }
        if (arg.find('=') != std::string::npos) continue;
        return arg;
    }
    return "";
}

std::vector<std::string> unknownOptions(const std::vector<std::string>& args) {
    std::vector<std::string> unknown;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const CliOption* option = findOption(arg);
        const bool has_inline_value = arg.find('=') != std::string::npos;
        if (option && !option->ignored) {
            if (option->value && !has_inline_value && i + 1 < args.size()) ++i;
            continue;
        }
        if (!arg.empty() && arg[0] == '-') unknown.push_back(arg);
        // Consume an ignored option's value so it is not taken as the wallpaper.
        if (option && option->value && !has_inline_value && i + 1 < args.size()) ++i;
    }
    return unknown;
}

}  // namespace cli_args
