#include "cli_args.hpp"

// Converts argv into a command vector, excluding the executable path itself.
std::vector<std::string> collect_args(int argc, char* argv[]) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }
  return args;
}

// Looks up the token following a named flag.
std::string get_arg_value(const std::vector<std::string>& args, const std::string& flag) {
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == flag && i + 1 < args.size()) {
      return args[i + 1];
    }
  }
  return "";
}
