#ifndef CAMPFIRE_CLI_ARGS_HPP
#define CAMPFIRE_CLI_ARGS_HPP

#include <string>
#include <vector>

// Converts raw argv input into a simple string vector for command parsing.
std::vector<std::string> collect_args(int argc, char* argv[]);

// Returns the value that follows a flag (for example: --name "Hades").
// If the flag is missing or has no value, this returns an empty string.
std::string get_arg_value(const std::vector<std::string>& args, const std::string& flag);

#endif
