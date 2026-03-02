#ifndef CAMPFIRE_LIBRARY_HPP
#define CAMPFIRE_LIBRARY_HPP

#include <string>
#include <vector>

// Represents one launcher entry persisted in the game library.
struct Game {
  std::string id;
  std::string name;
  std::string exe_path;
  std::string args;
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

// Scans well-known directories for likely game executables.
std::vector<ScannedGame> scan_common_game_dirs();

#endif
