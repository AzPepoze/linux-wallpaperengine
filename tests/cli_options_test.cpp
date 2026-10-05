#define SOKOL_ARGS_IMPL
#include "app/cli_options.h"

#include <sokol_args.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

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
        "\"transition\":\"crt\",\"web_transport\":\"snapshot\"}",
        config);
    fclose(config);

    // config.json fills in when the CLI flag is absent.
    {
        const CliOptions opts = parse({"app", "/wp"});
        CHECK(opts.scaling == "fill");
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

    chdir("/");
    return test::finish("cli options checks");
}
