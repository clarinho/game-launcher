#include "command_dispatcher.hpp"

#include "command_handlers.hpp"
#include "console_ui.hpp"

#include <string>
#include <vector>

namespace commands {
// Routes the first CLI token to one command handler.
int dispatch_command(const std::vector<std::string>& args) {
  if (args.empty() || args[0] == "--help" || args[0] == "help") {
    ui::print_help();
    return 0;
  }

  const std::string command = args[0];

  if (command == "add") {
    return handle_add(args);
  }
  if (command == "list") {
    return handle_list();
  }
  if (command == "scan") {
    return handle_scan();
  }
  if (command == "remove") {
    return handle_remove(args);
  }
  if (command == "launch") {
    return handle_launch(args);
  }
  if (command == "doctor") {
    return handle_doctor();
  }

  ui::print_error("Unknown command: " + command);
  ui::print_help();
  return 1;
}
}  // namespace commands
