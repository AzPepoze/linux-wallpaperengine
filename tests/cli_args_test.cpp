#include "app/cli_args.h"

#include <cstdio>
#include <string>
#include <vector>

#include "test_util.h"

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

    const V diagnose = {"app", "--diagnose", "--diagnose-frame", "10", "/wp"};
    CHECK(cli_args::positional(diagnose) == "/wp");
    CHECK(cli_args::optionValue(diagnose, {"--diagnose-frame"}, value) && value == "10");

    const V setProps = {"app", "--set-property", "a=1", "--set-property", "b=two words", "--set-property=c=3", "/wp"};
    CHECK(cli_args::positional(setProps) == "/wp");
    const std::vector<std::string> properties = cli_args::optionValues(setProps, {"--set-property"});
    CHECK(properties.size() == 3);
    CHECK(properties[0] == "a=1");
    CHECK(properties[1] == "b=two words");
    CHECK(properties[2] == "c=3");
    CHECK(cli_args::optionValues(launcher, {"--set-property"}).empty());

    const V transition = {"app", "--transition", "crt", "--transition-duration", "500", "/wp"};
    CHECK(cli_args::positional(transition) == "/wp");
    CHECK(cli_args::optionValue(transition, {"--transition"}, value) && value == "crt");
    CHECK(cli_args::optionValue(transition, {"--transition-duration"}, value) && value == "500");

    return test::finish("cli args checks");
}
