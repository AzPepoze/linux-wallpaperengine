#ifndef CLI_ARGS_H
#define CLI_ARGS_H

#include <string>
#include <vector>

// Argument scanning that does not depend on sokol_args, so it can be unit tested.
namespace cli_args {

// Options that consume the following argument unless written as --name=value.
bool takesValue(const std::string& arg);

// Value of the first matching spelling in `names`, from `--name value` or `--name=value`.
bool optionValue(const std::vector<std::string>& args, const std::vector<std::string>& names, std::string& out);

// Every value of a repeatable option, in order, from `--name value` or `--name=value`.
std::vector<std::string> optionValues(const std::vector<std::string>& args, const std::vector<std::string>& names);

bool hasFlag(const std::vector<std::string>& args, const std::vector<std::string>& names);

// First argument that is neither an option, an option value nor a key=value pair.
std::string positional(const std::vector<std::string>& args);

}  // namespace cli_args

#endif  // CLI_ARGS_H
