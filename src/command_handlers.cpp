#include "command_handlers.hpp"

#include "cli_args.hpp"
#include "console_ui.hpp"
#include "library.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {
std::string normalize_path_for_compare(const std::string& path_text) {
  std::error_code ec;
  fs::path path(path_text);
  const fs::path absolute_path = fs::absolute(path, ec);
  if (ec) {
    return ui::to_lower(path.lexically_normal().string());
  }

  const fs::path normalized = fs::weakly_canonical(absolute_path, ec);
  if (ec) {
    return ui::to_lower(absolute_path.lexically_normal().string());
  }

  return ui::to_lower(normalized.string());
}

std::string normalize_path_for_storage(const std::string& path_text) {
  std::error_code ec;
  fs::path path(path_text);
  const fs::path absolute_path = fs::absolute(path, ec);
  if (ec) {
    return path.lexically_normal().string();
  }

  const fs::path normalized = fs::weakly_canonical(absolute_path, ec);
  if (ec) {
    return absolute_path.lexically_normal().string();
  }

  return normalized.string();
}

bool is_exe_extension(const fs::path& path) {
  return ui::to_lower(path.extension().string()) == ".exe";
}

const Game* find_game_by_id_or_name(
    const std::vector<Game>& games,
    const std::string& id,
    const std::string& name) {
  if (!id.empty()) {
    for (const Game& game : games) {
      if (game.id == id) {
        return &game;
      }
    }
    return nullptr;
  }

  const std::string wanted = ui::to_lower(name);
  for (const Game& game : games) {
    if (ui::to_lower(game.name) == wanted) {
      return &game;
    }
  }

  return nullptr;
}

#ifdef _WIN32
int start_process(const std::string& exe_path, const std::string& launch_args) {
  std::string command_line = "\"" + exe_path + "\"";
  if (!launch_args.empty()) {
    command_line += " " + launch_args;
  }

  STARTUPINFOA startup_info{};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info{};

  std::vector<char> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back('\0');

  const BOOL ok = CreateProcessA(
      nullptr,
      mutable_command.data(),
      nullptr,
      nullptr,
      FALSE,
      0,
      nullptr,
      nullptr,
      &startup_info,
      &process_info);

  if (!ok) {
    ui::print_error("Failed to launch game. Windows error: " + std::to_string(GetLastError()));
    return 1;
  }

  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  return 0;
}
#else
int start_process(const std::string& exe_path, const std::string& launch_args) {
  std::string command = "\"" + exe_path + "\"";
  if (!launch_args.empty()) {
    command += " " + launch_args;
  }
  command += " &";
  if (std::system(command.c_str()) != 0) {
    ui::print_error("Failed to launch game process.");
    return 1;
  }
  return 0;
}
#endif
}  // namespace

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

  std::error_code ec;
  const fs::path exe_path(exe);
  if (!fs::exists(exe_path, ec) || ec) {
    ui::print_error("Executable does not exist: " + exe);
    return 1;
  }
  if (!fs::is_regular_file(exe_path, ec) || ec) {
    ui::print_error("Executable path is not a file: " + exe);
    return 1;
  }
  if (!is_exe_extension(exe_path)) {
    ui::print_error("Executable must be a .exe file: " + exe);
    return 1;
  }

  const std::string normalized_exe = normalize_path_for_storage(exe);
  const std::string normalized_key = normalize_path_for_compare(normalized_exe);
  for (const Game& existing : load_games()) {
    if (normalize_path_for_compare(existing.exe_path) == normalized_key) {
      ui::print_error("A game with this executable already exists (id: " + existing.id + ")");
      return 1;
    }
  }

  const Game game = add_game(name, normalized_exe, launch_args);
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

// Runs non-destructive diagnostics for the local Campfire runtime.
int handle_doctor() {
  bool healthy = true;
  ui::print_info("Running Campfire diagnostics...");

  const char* local_app_data = std::getenv("LOCALAPPDATA");
  if (local_app_data != nullptr && local_app_data[0] != '\0') {
    ui::print_ok(std::string("LOCALAPPDATA: ") + local_app_data);
  } else {
    ui::print_info("LOCALAPPDATA is not set. Campfire will use a local ./data fallback.");
  }

  const std::string library_path = game_library_file_path();
  ui::print_info("Library file: " + library_path);

  std::vector<Game> games;
  try {
    games = load_games();
    ui::print_ok("Library is readable. Entries: " + std::to_string(games.size()));
  } catch (const std::exception& ex) {
    ui::print_error(std::string("Library read failed: ") + ex.what());
    return 1;
  }

  std::unordered_set<std::string> seen_exes;
  int duplicate_exe_count = 0;
  int missing_exe_count = 0;
  for (const Game& game : games) {
    const std::string exe_key = normalize_path_for_compare(game.exe_path);
    if (seen_exes.find(exe_key) != seen_exes.end()) {
      ++duplicate_exe_count;
    } else {
      seen_exes.insert(exe_key);
    }

    std::error_code ec;
    if (!fs::exists(game.exe_path, ec) || ec || !fs::is_regular_file(game.exe_path, ec)) {
      ++missing_exe_count;
    }
  }

  if (duplicate_exe_count == 0) {
    ui::print_ok("No duplicate executable paths in library.");
  } else {
    ui::print_error("Duplicate executable entries detected: " + std::to_string(duplicate_exe_count));
    healthy = false;
  }

  if (missing_exe_count == 0) {
    ui::print_ok("All library executables exist on disk.");
  } else {
    ui::print_error("Missing executable files: " + std::to_string(missing_exe_count));
    healthy = false;
  }

  const std::vector<std::string> roots = common_scan_roots();
  int present_root_count = 0;
  for (const std::string& root : roots) {
    std::error_code ec;
    if (fs::exists(root, ec) && fs::is_directory(root, ec)) {
      ++present_root_count;
    }
  }
  ui::print_info(
      "Scan roots present: " + std::to_string(present_root_count) + "/" + std::to_string(roots.size()));

  if (healthy) {
    ui::print_ok("Doctor checks passed.");
    return 0;
  }

  ui::print_error("Doctor found issues.");
  return 1;
}

// Launches one game by ID or exact name match.
int handle_launch(const std::vector<std::string>& args) {
  const std::string id = get_arg_value(args, "--id");
  const std::string name = get_arg_value(args, "--name");
  if ((id.empty() && name.empty()) || (!id.empty() && !name.empty())) {
    ui::print_error("launch requires exactly one of --id or --name");
    return 1;
  }

  const std::vector<Game> games = load_games();
  const Game* game = find_game_by_id_or_name(games, id, name);
  if (!game) {
    ui::print_error("No game found for launch target.");
    return 1;
  }

  std::error_code ec;
  if (!fs::exists(game->exe_path, ec) || ec || !fs::is_regular_file(game->exe_path, ec)) {
    ui::print_error("Executable not found on disk: " + game->exe_path);
    return 1;
  }

  const int launch_result = start_process(game->exe_path, game->args);
  if (launch_result == 0) {
    ui::print_ok("Launched: " + game->name);
  }

  return launch_result;
}
}  // namespace commands
