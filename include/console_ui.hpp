#ifndef CAMPFIRE_CONSOLE_UI_HPP
#define CAMPFIRE_CONSOLE_UI_HPP

#include "library.hpp"

#include <string>
#include <vector>

namespace ui {
// Prints the help/usage screen with ASCII banner.
void print_help();

// Prints success/info/error messages with consistent styling.
void print_ok(const std::string& msg);
void print_info(const std::string& msg);
void print_error(const std::string& msg);

// Prints details after a single game is added.
void print_add_result(const Game& game);

// Prints tabular output for library and scan results.
void print_list_table(const std::vector<Game>& games);
void print_scan_table(std::vector<ScannedGame> found);

// Prompts the user for a yes/no choice. Only "y" and "yes" are treated as true.
bool prompt_yes_no(const std::string& prompt);

// Normalizes text for case-insensitive comparisons.
std::string to_lower(std::string value);
}  // namespace ui

#endif
