#include "app.hpp"

#include "command_dispatcher.hpp"
#include "console_ui.hpp"

#include <exception>
#include <vector>

// Wraps command dispatch in a top-level exception boundary.
int run_app(const std::vector<std::string>& args) {
  try {
    return commands::dispatch_command(args);
  } catch (const std::exception& ex) {
    ui::print_error(ex.what());
    return 1;
  }
}
