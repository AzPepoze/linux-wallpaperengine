#include "app/cli_args.h"

#include <algorithm>

namespace cli_args {

namespace {
const char* const kValueOptions[] = {"--gpu",
                                     "-gpu",
                                     "--pkg",
                                     "-pkg",
                                     "--extract-dir",
                                     "-extract-dir",
                                     "--assets-dir",
                                     "-f",
                                     "--fps",
                                     "--scaling",
                                     "--clamp",
                                     "-r",
                                     "--screen-root",
                                     "--layer",
                                     "--layer-size",
                                     "--layer-anchor",
                                     "--volume",
                                     "--disable-effects",
                                     "--diagnose-frame",
                                     "--particle-debug-velocity-scale",
                                     "--particle-debug-max-particles"};
}  // namespace

bool takesValue(const std::string& arg) {
    return std::any_of(std::begin(kValueOptions), std::end(kValueOptions),
                       [&](const char* option) { return arg == option; });
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

}  // namespace cli_args
