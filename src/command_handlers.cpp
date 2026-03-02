#include "command_handlers.hpp"

#include "cli_args.hpp"
#include "console_ui.hpp"
#include "library.hpp"

#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace commands {
// Parses and validates `add` flags, then writes a new library entry.
int handle_add(const std::vector<std::string>& args) {
  const std::string name = get_arg_value(args, "--name");
  const std::string exe = get_arg_value(args, "--exe");
  const std::string launch_args = get_arg_value(args, "--args");

  if (name.empty() || exe.empty()) {
    ui::print_error("add requires --name and --exe");
    return 1;
  }

  const Game game = add_game(name, exe, launch_args);
  ui::print_add_result(game);
  return 0;
}

// Reads and prints all games currently stored in the local library file.
int handle_list() {
  const std::vector<Game> games = load_games();
  if (games.empty()) {
    ui::print_info("No games yet. Add one with `campfire add ...`");
    return 0;
  }

  ui::print_list_table(games);
  return 0;
}

// Scans known directories and optionally imports discovered executables.
int handle_scan() {
  const std::vector<ScannedGame> found = scan_common_game_dirs();
  if (found.empty()) {
    ui::print_info("No games found in common directories.");
    return 0;
  }

  ui::print_scan_table(found);
  std::cout << '\n';

  if (!ui::prompt_yes_no("Add found games to library?")) {
    ui::print_info("Scan complete. No games were added.");
    return 0;
  }

  const std::vector<Game> existing_games = load_games();
  std::unordered_set<std::string> existing_exe_paths;
  for (const Game& game : existing_games) {
    existing_exe_paths.insert(ui::to_lower(game.exe_path));
  }

  int added_count = 0;
  int skipped_count = 0;
  for (const ScannedGame& scanned : found) {
    const std::string exe_key = ui::to_lower(scanned.exe_path);
    if (existing_exe_paths.find(exe_key) != existing_exe_paths.end()) {
      ++skipped_count;
      continue;
    }

    add_game(scanned.name, scanned.exe_path, "");
    existing_exe_paths.insert(exe_key);
    ++added_count;
  }

  ui::print_ok("Import complete.");
  std::cout << "added: " << added_count << '\n'
            << "skipped (already in library): " << skipped_count << '\n';
  return 0;
}

// Removes one library entry by ID.
int handle_remove(const std::vector<std::string>& args) {
  const std::string id = get_arg_value(args, "--id");
  if (id.empty()) {
    ui::print_error("remove requires --id");
    return 1;
  }

  if (!remove_game(id)) {
    ui::print_error("No game found with id: " + id);
    return 1;
  }

  ui::print_ok("Removed game: " + id);
  return 0;
}
}  // namespace commands
