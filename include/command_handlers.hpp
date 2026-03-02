#ifndef CAMPFIRE_COMMAND_HANDLERS_HPP
#define CAMPFIRE_COMMAND_HANDLERS_HPP

#include <string>
#include <vector>

namespace commands {
// Handles `campfire add` command parsing and persistence.
int handle_add(const std::vector<std::string>& args);

// Handles `campfire list` command output.
int handle_list();

// Handles `campfire scan` and optional interactive import.
int handle_scan();

// Handles `campfire remove` command parsing and deletion.
int handle_remove(const std::vector<std::string>& args);
}  // namespace commands

#endif
