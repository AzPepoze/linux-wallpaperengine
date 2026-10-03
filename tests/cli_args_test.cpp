#include "app/cli_args.h"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

int main() {
    using V = std::vector<std::string>;
    const V launcher = {"app",   "/wp/dir", "-r",        "DP-4",         "-f",
                        "60",    "-s",      "--scaling", "fill",         "--clamp",
                        "clamp", "--layer", "bottom",    "--assets-dir", "/engine/assets"};
    CHECK(cli_args::positional(launcher) == "/wp/dir");
    std::string value;
    CHECK(cli_args::optionValue(launcher, {"-f", "--fps"}, value) && value == "60");
    CHECK(cli_args::optionValue(launcher, {"--assets-dir"}, value) && value == "/engine/assets");
    CHECK(cli_args::optionValue(launcher, {"-r", "--screen-root"}, value) && value == "DP-4");
    CHECK(cli_args::hasFlag(launcher, {"-s", "--silent"}));
    CHECK(!cli_args::hasFlag(launcher, {"--mute"}));

    const V trailing = {"app", "--fps=30", "--assets-dir=/a", "/wp"};
    CHECK(cli_args::positional(trailing) == "/wp");
    CHECK(cli_args::optionValue(trailing, {"--fps"}, value) && value == "30");

    const V optionsOnly = {"app", "--no-ui", "--gpu", "1"};
    CHECK(cli_args::positional(optionsOnly).empty());
    CHECK(!cli_args::optionValue(optionsOnly, {"--assets-dir"}, value));

    const V keyValue = {"app", "pkg=/x.pkg", "/wp"};
    CHECK(cli_args::positional(keyValue) == "/wp");

    const V layerDebug = {"app", "--layer-size", "320x180", "--layer-anchor", "top-left", "/wp"};
    CHECK(cli_args::positional(layerDebug) == "/wp");
    CHECK(cli_args::optionValue(layerDebug, {"--layer-size"}, value) && value == "320x180");

    if (failures == 0) std::printf("cli args checks passed\n");
    return failures == 0 ? 0 : 1;
}
