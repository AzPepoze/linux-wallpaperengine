#define SOKOL_ARGS_IMPL
#include "app/cli_options.h"

#include <sokol_args.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include "shared/core/build_config.h"
#include "shared/core/config_candidates.h"
#include "test_util.h"

namespace {
CliOptions parse(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (const std::string& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    return CliOptions::parse(static_cast<int>(argv.size()), argv.data());
}
}  // namespace

int main() {
    char dir[] = "/tmp/lwe-cli-options-XXXXXX";
    CHECK(mkdtemp(dir) != nullptr);
    CHECK(chdir(dir) == 0);

    FILE* config = fopen("config.json", "w");
    CHECK(config != nullptr);
    fputs(
        "{\"scaling_mode\":\"fill\",\"parallax_smoothing\":0.25,\"parallax_scale\":40.0,"
        "\"transition\":\"crt\",\"web_transport\":\"snapshot\",\"effect_resolution\":\"native\"}",
        config);
    fclose(config);

    // config.json fills in when the CLI flag is absent.
    {
        const CliOptions opts = parse({"app", "/wp"});
        CHECK(opts.scaling == "fill");
        CHECK(opts.resolution.mode == resolution::Setting::Mode::Native);
        CHECK(std::find(opts.startup_options.begin(), opts.startup_options.end(),
                        "scaling_mode=fill (source: config)") != opts.startup_options.end());
        CHECK(std::find(opts.startup_options.begin(), opts.startup_options.end(), "fps_limit=0 (source: default)") !=
              opts.startup_options.end());
        CHECK(opts.parallax.smoothing == 0.25f);
        CHECK(opts.parallax.scale == 40.0f);
        CHECK(opts.transition.effect == "crt");
        CHECK(opts.web.transport == WebTransport::Snapshot);
    }

    // A CLI flag overrides the config.
    {
        const CliOptions opts = parse({"app", "--scaling", "fit", "--web-transport", "dma-buf", "/wp"});
        CHECK(opts.scaling == "fit");
        CHECK(std::find(opts.startup_options.begin(), opts.startup_options.end(), "scaling_mode=fit (source: CLI)") !=
              opts.startup_options.end());
        CHECK(opts.web.transport == WebTransport::DmaBuf);
    }

    {
        const CliOptions opts = parse({"app", "--resolution", "native", "/wp"});
        CHECK(opts.resolution.mode == resolution::Setting::Mode::Native);
        CHECK(opts.wallpaper_arg == "/wp");
    }

    {
        const CliOptions opts = parse({"app", "--resolution", "auto", "--performance-profile", "/wp"});
        CHECK(opts.resolution.mode == resolution::Setting::Mode::Auto);
        CHECK(opts.performance_profile);
        CHECK(opts.wallpaper_arg == "/wp");
    }

    // The older --effect-resolution spelling still works.
    {
        const CliOptions opts = parse({"app", "--effect-resolution", "native", "/wp"});
        CHECK(opts.resolution.mode == resolution::Setting::Mode::Native);
        CHECK(opts.wallpaper_arg == "/wp");
    }

    {
        const CliOptions opts = parse({"app", "--resolution", "3840x2160", "/wp"});
        CHECK(opts.resolution.mode == resolution::Setting::Mode::Fixed);
        CHECK(opts.resolution.width == 3840);
        CHECK(opts.resolution.height == 2160);
    }

    // An unknown value falls back to auto.
    {
        const CliOptions opts = parse({"app", "--resolution", "bogus", "/wp"});
        CHECK(opts.resolution.mode == resolution::Setting::Mode::Auto);
    }

    {
        const CliOptions opts = parse({"app", "--window", "-100x20x1280x720", "/wp"});
        CHECK(opts.window.set);
        CHECK(opts.window.x == -100);
        CHECK(opts.window.y == 20);
        CHECK(opts.window.width == 1280);
        CHECK(opts.window.height == 720);
        CHECK(opts.wallpaper_arg == "/wp");
    }

    // Upstream names that are accepted but do nothing; their values never become the wallpaper path.
    {
        const CliOptions opts = parse({"app", "--bg", "123", "/wp"});
        CHECK(opts.wallpaper_arg == "/wp");
    }
    {
        const CliOptions opts = parse({"app", "--clamping", "border", "/wp"});
        CHECK(opts.wallpaper_arg == "/wp");
    }
    {
        const CliOptions opts = parse({"app", "--list-properties", "/wp"});
        CHECK(opts.wallpaper_arg == "/wp");
    }

    // --quiet beats --log-level; an unknown level keeps the build default.
    {
        CHECK(parse({"app", "--log-level", "warn", "/wp"}).log_level == LOG_LEVEL_WARN);
        CHECK(parse({"app", "--quiet", "--log-level", "debug", "/wp"}).log_level == LOG_LEVEL_ERROR);
        CHECK(parse({"app", "-q", "/wp"}).log_level == LOG_LEVEL_ERROR);
        CHECK(parse({"app", "--log-level", "loud", "/wp"}).log_level ==
              (DEBUG_BUILD ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO));
    }

    {
        const CliOptions opts = parse({"app", "--version", "--list-outputs", "--list-transitions", "/wp"});
        CHECK(opts.version && opts.list_outputs && opts.list_transitions);
        CHECK(opts.wallpaper_arg == "/wp");
    }

    {
        const CliOptions opts = parse({"app", "-l", "--disable-parallax", "--disable-mouse", "/wp"});
        CHECK(opts.list_properties && opts.disable_parallax && opts.disable_mouse);
        CHECK(opts.wallpaper_arg == "/wp");
    }

    // --config replaces the search and is also read for log_level.
    {
        FILE* other = fopen("other.json", "w");
        CHECK(other != nullptr);
        fputs("{\"scaling_mode\":\"stretch\",\"log_level\":\"warn\"}", other);
        fclose(other);
        const CliOptions opts = parse({"app", "--config", "other.json", "/wp"});
        CHECK(opts.config_path == "other.json");
        CHECK(opts.scaling == "stretch");
        CHECK(opts.log_level == LOG_LEVEL_WARN);
        CHECK(configPathOverride() == "other.json");
        configPathOverride().clear();
    }

    WindowGeometry window;
    CHECK(!parseWindowGeometry("800x450", window));
    CHECK(!parseWindowGeometry("800x0x0x450", window));
    CHECK(!parseWindowGeometry("axbxcxd", window));
    CHECK(!parseWindowGeometry("0x0x800x450x1", window));
    CHECK(!window.set);
    CHECK(parseWindowGeometry("0x0x800x450", window));
    CHECK(window.set && window.width == 800 && window.height == 450);

    chdir("/");
    {
        const auto opts = parse({"test", "--intro-zoom", "1.08", "--intro-duration", "4", "/wp"});
        CHECK(opts.intro_zoom == 1.08f && opts.intro_duration == 4.0f);
        CHECK(opts.wallpaper_arg == "/wp");
        const auto invalid = parse({"test", "--intro-zoom", "nan", "--intro-duration", "-1", "/wp"});
        CHECK(invalid.intro_zoom == 1.0f && invalid.intro_duration == 4.0f);
    }
    return test::finish("cli options checks");
}
