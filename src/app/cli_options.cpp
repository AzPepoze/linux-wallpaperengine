#include "app/cli_options.h"

#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "app/cli_args.h"
#include "shared/core/build_config.h"
#include "sokol_args.h"

namespace {
// sokol_args strips a single leading dash but keeps "--", so boolean flags
// accept both spellings explicitly.
bool hasFlag(const char* name) {
    return sargs_exists(name);
}

bool hasDashedFlag(const char* name) {
    return sargs_exists(name) || sargs_exists((std::string("--") + name).c_str());
}

std::string gpuArg(int argc, char* argv[]) {
    std::string gpu;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--gpu") == 0 || strcmp(argv[i], "-gpu") == 0) {
            if (i + 1 < argc) gpu = argv[i + 1];
        } else if (strncmp(argv[i], "--gpu=", 6) == 0) {
            gpu = argv[i] + 6;
        }
    }
    if (hasFlag("gpu") && sargs_value("gpu")) gpu = sargs_value("gpu");
    return gpu;
}

std::string flagValue(const char* name) {
    const char* value = sargs_value(name);
    return value ? value : "";
}

#if DEBUG_BUILD
// This sokol_args version keeps leading dashes in keys and prefixes values with '=', so probe every spelling.
bool hasAnySpelling(const char* name) {
    const std::string dashed = std::string("--") + name;
    std::string underscored = name;
    for (char& c : underscored)
        if (c == '-') c = '_';
    return sargs_exists(name) || sargs_exists(dashed.c_str()) || sargs_exists(underscored.c_str()) ||
           sargs_exists(("--" + underscored).c_str());
}

std::string valueAnySpelling(const char* name) {
    const std::string dashed = std::string("--") + name;
    for (const char* key : {name, dashed.c_str()}) {
        if (!sargs_exists(key)) continue;
        std::string value = sargs_value_def(key, "");
        if (!value.empty() && value.front() == '=') value.erase(value.begin());
        return value;
    }
    return "";
}
#endif

bool envEnabled(const char* name) {
    const char* value = getenv(name);
    return value && value[0] && strcmp(value, "0") != 0;
}
}  // namespace

CliOptions CliOptions::parse(int argc, char* argv[]) {
    sargs_desc a_desc = {};
    a_desc.argc = argc;
    a_desc.argv = argv;
    a_desc.max_args = 64;
    sargs_setup(&a_desc);

    CliOptions opts;
    opts.gpu = gpuArg(argc, argv);
    opts.list_gpus = hasDashedFlag("list-gpus");
#if DEBUG_BUILD
    opts.sandbox = hasDashedFlag("sandbox");
    opts.no_ui = hasDashedFlag("no-ui");
    opts.diagnostics.enabled = hasDashedFlag("diagnose") || hasDashedFlag("diagnostics");
    opts.diagnostics.disable_effects = valueAnySpelling("disable-effects");
    opts.diagnostics.disable_particles = hasAnySpelling("disable-particles");
    opts.diagnostics.disable_bloom = hasAnySpelling("disable-bloom");
    opts.diagnostics.final_only = hasDashedFlag("diagnose-final-only");
    opts.diagnostics.exit_after_diagnose = hasDashedFlag("exit-after-diagnose");
    opts.diagnostics.deterministic = hasDashedFlag("diagnose-deterministic");
#endif
    const std::vector<std::string> args(argv, argv + argc);
    opts.no_audio = hasDashedFlag("no-audio") || envEnabled("LWE_NO_AUDIO") || opts.no_ui || opts.diagnostics.enabled ||
                    cli_args::hasFlag(args, {"-s", "--silent", "--mute"});
    opts.video_ram = cli_args::hasFlag(args, {"--video-ram"}) || envEnabled("LWE_VIDEO_RAM");
#if DEBUG_BUILD
    std::string capture_frame;
    if (cli_args::optionValue(args, {"--diagnose-frame"}, capture_frame)) {
        opts.diagnostics.target_frame = atoi(capture_frame.c_str());
    }
#endif
    cli_args::optionValue(args, {"--assets-dir"}, opts.assets_dir);
    cli_args::optionValue(args, {"--scaling"}, opts.scaling);
    cli_args::optionValue(args, {"--clamp"}, opts.clamp);
    cli_args::optionValue(args, {"-r", "--screen-root"}, opts.screen_root);
    cli_args::optionValue(args, {"--layer"}, opts.layer);
#if DEBUG_BUILD
    cli_args::optionValue(args, {"--layer-size"}, opts.layer_size);
    cli_args::optionValue(args, {"--layer-anchor"}, opts.layer_anchor);
#endif
    std::string fps;
    if (cli_args::optionValue(args, {"-f", "--fps"}, fps)) opts.fps_limit = atoi(fps.c_str());
    opts.cover = hasFlag("cover");
    opts.particle_debug_bounds = hasFlag("particle-debug-bounds") || hasFlag("particle-debug");
    opts.particle_debug_velocity = hasFlag("particle-debug-velocity") || hasFlag("particle-debug");
    if (hasFlag("particle-debug-velocity-scale"))
        opts.particle_debug_velocity_scale = static_cast<float>(atof(sargs_value("particle-debug-velocity-scale")));
    if (hasFlag("particle-debug-max-particles"))
        opts.particle_debug_max_particles = atoi(sargs_value("particle-debug-max-particles"));

    opts.extract_only = hasDashedFlag("extract-only");
    opts.has_extract_dir = hasFlag("extract-dir");
    if (opts.has_extract_dir) opts.extract_dir = flagValue("extract-dir");

    if (!opts.sandbox && hasFlag("pkg")) {
        opts.wallpaper_arg = flagValue("pkg");
        opts.pkg_flag = true;
    } else if (!opts.sandbox) {
        opts.wallpaper_arg = cli_args::positional(args);
    }
    sargs_shutdown();
    return opts;
}
