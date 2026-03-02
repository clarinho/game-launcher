#ifndef CAMPFIRE_LIBRARY_HPP
#define CAMPFIRE_LIBRARY_HPP

#include <string>
#include <vector>

// Represents one launcher entry persisted in the game library.
struct Game {
  std::string id;
  std::string name;
  std::string exe_path;
  std::vector<std::string> exe_candidates;
  std::string args;
  long long play_count = 0;
  long long total_play_seconds = 0;
};

// Represents one discovered executable during folder scanning.
struct ScannedGame {
  std::string name;
  std::string exe_path;
};

// Loads all persisted games from storage.
std::vector<Game> load_games();

// Adds a game and returns the created record with generated ID.
Game add_game(const std::string& name, const std::string& exe_path, const std::string& args);

// Removes a game by ID. Returns true when a game was removed.
bool remove_game(const std::string& id);

// Updates one game by matching ID. Returns true when a game was updated.
bool update_game(const Game& updated);

// Scans well-known directories for likely game executables.
std::vector<ScannedGame> scan_common_game_dirs();

// Returns the resolved path to the library TSV file.
std::string game_library_file_path();

// Returns the configured root directories used by `scan`.
std::vector<std::string> common_scan_roots();

#endif
