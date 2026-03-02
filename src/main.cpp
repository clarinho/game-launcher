#include "app.hpp"
#include "cli_args.hpp"

#include <vector>

// Minimal process entrypoint: parse CLI args and delegate execution.
int main(int argc, char* argv[]) {
  const std::vector<std::string> args = collect_args(argc, argv);
  return run_app(args);
}
