#include "app/cli_options.h"

#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "app/cli_args.h"
#include "app/flag_config.h"
#include "shared/core/build_config.h"
#include "shared/core/logger.h"
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
    opts.diagnostics.disable_particles = hasDashedFlag("disable-particles");
    opts.diagnostics.disable_bloom = hasAnySpelling("disable-bloom");
    opts.diagnostics.final_only = hasDashedFlag("diagnose-final-only");
    opts.diagnostics.exit_after_diagnose = hasDashedFlag("exit-after-diagnose");
    opts.diagnostics.deterministic = hasDashedFlag("diagnose-deterministic");
#endif
    const std::vector<std::string> args(argv, argv + argc);
    auto record = [&](const std::string& key, const std::string& value, const char* source) {
        opts.startup_options.push_back(key + "=" + (value.empty() ? "<unset>" : value) + " (source: " + source + ")");
    };
    // CLI flags override config.json, which overrides the defaults.
    auto resolve = [&](const std::vector<std::string>& names, const char* key, const std::string& fallback) {
        std::string value;
        if (cli_args::optionValue(args, names, value)) {
            record(key, value, "CLI");
            return value;
        }
        const std::string configured = flag_config::string(key);
        record(key, configured.empty() ? fallback : configured, configured.empty() ? "default" : "config");
        return configured.empty() ? fallback : configured;
    };
    auto resolveInt = [&](const std::vector<std::string>& names, const char* key, int fallback) {
        std::string value;
        if (cli_args::optionValue(args, names, value)) {
            record(key, std::to_string(atoi(value.c_str())), "CLI");
            return atoi(value.c_str());
        }
        const int configured = flag_config::integer(key);
        record(key, std::to_string(configured != 0 ? configured : fallback), configured != 0 ? "config" : "default");
        return configured != 0 ? configured : fallback;
    };
    auto resolveReal = [&](const char* key, float fallback) {
        const float configured = flag_config::real(key);
        record(key, std::to_string(configured != 0.0f ? configured : fallback),
               configured != 0.0f ? "config" : "default");
        return configured != 0.0f ? configured : fallback;
    };

    opts.help = cli_args::hasFlag(args, {"-h", "--help"});
    opts.no_audio = hasDashedFlag("no-audio") || opts.no_ui || opts.diagnostics.enabled ||
                    cli_args::hasFlag(args, {"-s", "--silent", "--mute"});
    opts.video_ram = cli_args::hasFlag(args, {"--video-ram"});
    opts.script_profile = cli_args::hasFlag(args, {"--script-profile"});
#if DEBUG_BUILD
    std::string capture_frame;
    if (cli_args::optionValue(args, {"--diagnose-frame"}, capture_frame)) {
        opts.diagnostics.target_frame = atoi(capture_frame.c_str());
    }
#endif
    cli_args::optionValue(args, {"--assets-dir"}, opts.assets_dir);
    opts.scaling = resolve({"--scaling"}, "scaling_mode", "");
    opts.parallax.smoothing = resolveReal("parallax_smoothing", 0.0f);
    opts.parallax.scale = resolveReal("parallax_scale", 0.0f);
    cli_args::optionValue(args, {"--clamp"}, opts.clamp);
    cli_args::optionValue(args, {"-r", "--screen-root"}, opts.screen_root);
    cli_args::optionValue(args, {"--layer"}, opts.layer);
    opts.no_control = hasDashedFlag("no-control");
    for (const std::string& entry : cli_args::optionValues(args, {"--set-property"})) {
        const size_t equals = entry.find('=');
        if (equals == std::string::npos) continue;
        opts.set_properties.emplace_back(entry.substr(0, equals), entry.substr(equals + 1));
    }
#if DEBUG_BUILD
    cli_args::optionValue(args, {"--layer-size"}, opts.layer_size);
    cli_args::optionValue(args, {"--layer-anchor"}, opts.layer_anchor);
#endif
    std::string fps;
    if (cli_args::optionValue(args, {"-f", "--fps"}, fps)) opts.fps_limit = atoi(fps.c_str());
    // On by default (localhost only); --no-web-devtools opts out.
    opts.web.devtools = !hasDashedFlag("no-web-devtools");
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
    opts.transition.effect = resolve({"--transition"}, "transition", "");
    opts.transition.duration_ms = resolveInt({"--transition-duration"}, "transition_duration_ms", 0);
    opts.transition.mode = resolve({"--transition-mode"}, "transition_mode", "");

    opts.web.devtools_port = resolveInt({"--web-devtools-port"}, "web_devtools_port", 9222);
    if (opts.web.devtools_port < 1 || opts.web.devtools_port > 65535) {
        opts.web.devtools_port = 9222;
        record("web_devtools_port_effective", "9222", "invalid port fallback");
    }
    opts.web.devtools_browser = resolve({"--web-devtools-browser"}, "web_devtools_browser", "");

    const std::string transport = resolve({"--web-transport"}, "web_transport", "auto");
    if (!parseWebTransport(transport, opts.web.transport)) {
        LOG_E("Invalid web_transport '%s' (expected: auto, dma-buf, off-screen, snapshot)", transport.c_str());
        exit(EXIT_FAILURE);
    }
    auto cliValue = [&](const char* key, const std::string& value, const std::vector<std::string>& names) {
        std::string supplied;
        record(key, value, cli_args::optionValue(args, names, supplied) ? "CLI" : "default");
    };
    cliValue("assets_dir", opts.assets_dir, {"--assets-dir"});
    cliValue("screen_root", opts.screen_root, {"-r", "--screen-root"});
    cliValue("layer", opts.layer.empty() ? "background" : opts.layer, {"--layer"});
    cliValue("clamp", opts.clamp, {"--clamp"});
    cliValue("fps_limit", std::to_string(opts.fps_limit), {"-f", "--fps"});
    record("gpu", opts.gpu.empty() ? "auto" : opts.gpu, opts.gpu.empty() ? "default" : "CLI");
    record("audio", opts.no_audio ? "disabled" : "enabled",
           hasDashedFlag("no-audio") || cli_args::hasFlag(args, {"-s", "--silent", "--mute"})
               ? "CLI"
               : (opts.no_ui || opts.diagnostics.enabled ? "diagnostics/no-ui" : "default"));
    record("cover", opts.cover ? "true" : "false", opts.cover ? "CLI" : "default");
    record("control", opts.no_control ? "disabled" : "enabled", opts.no_control ? "CLI" : "default");
    record("video_ram", opts.video_ram ? "true" : "false", opts.video_ram ? "CLI" : "default");
    record("script_profile", opts.script_profile ? "true" : "false", opts.script_profile ? "CLI" : "default");
    record("web_devtools", opts.web.devtools ? "enabled" : "disabled", opts.web.devtools ? "default" : "CLI");
    record("diagnostics", opts.diagnostics.enabled ? "enabled" : "disabled",
           opts.diagnostics.enabled ? "CLI" : "default");
    sargs_shutdown();
    return opts;
}

void CliOptions::logResolvedOptions() const {
    const std::string config_path = flag_config::loadedPath();
    LOG_TAG_I("OPTIONS", "Config: %s; precedence: CLI > config > default",
              config_path.empty() ? "<none>" : config_path.c_str());
    for (const std::string& option : startup_options) LOG_TAG_I("OPTIONS", "%s", option.c_str());
    const char* effective_scaling =
        cover || scaling == "default" || scaling == "fill" ? "cover" : (scaling == "stretch" ? "stretch" : "fit");
    LOG_TAG_I("OPTIONS", "Effective scaling: %s%s", effective_scaling, cover ? " (--cover override)" : "");
}
