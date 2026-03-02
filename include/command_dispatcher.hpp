#ifndef CAMPFIRE_COMMAND_DISPATCHER_HPP
#define CAMPFIRE_COMMAND_DISPATCHER_HPP

#include <string>
#include <vector>

namespace commands {
// Routes parsed CLI arguments to the matching command handler.
int dispatch_command(const std::vector<std::string>& args);
}  // namespace commands

#endif
