#ifndef CLI_ARGS_H
#define CLI_ARGS_H

#include <stdio.h>

#include <string>
#include <vector>

// Argument scanning that does not depend on sokol_args, so it can be unit tested.
namespace cli_args {

enum class CliGroup {
    Wallpaper,
    Graphics,
    Display,
    Transition,
    Control,
    Audio,
    Web,
    Diagnostics,
    Particles,
    Info,
    Compatibility,
    Count
};

// Options in a Debug group row are only listed by debug builds.
enum class CliBuild { All, Debug };

struct CliOption {
    const char* names[4];  // nullptr-terminated spellings: {"-f", "--fps", nullptr}
    const char* value;     // nullptr => flag; else placeholder, e.g. "<n>"
    CliBuild build = CliBuild::All;
    const char* description = "";
    bool ignored = false;  // recognized for compatibility but reported as unsupported
};

// A help section and the options it owns; options are sentinel-terminated.
struct CliGroupDef {
    CliGroup group;
    const char* title;
    const CliOption* options;
};

// The single declaration site, indexed by CliGroup; length is (int)CliGroup::Count.
const CliGroupDef* cliGroups();

// Prints the grouped option reference. Debug-only rows are hidden in release.
void printHelp(FILE* out);

// Options that consume the following argument unless written as --name=value.
bool takesValue(const std::string& arg);

bool optionValue(const std::vector<std::string>& args, const std::vector<std::string>& names, std::string& out);

std::vector<std::string> optionValues(const std::vector<std::string>& args, const std::vector<std::string>& names);

bool hasFlag(const std::vector<std::string>& args, const std::vector<std::string>& names);

std::string positional(const std::vector<std::string>& args);

// Unimplemented option-shaped arguments; values of known options are excluded.
std::vector<std::string> unknownOptions(const std::vector<std::string>& args);

}  // namespace cli_args

#endif  // CLI_ARGS_H
