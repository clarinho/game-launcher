#include "library.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

// Resolves the storage file location. Prefer a stable user path over CWD.
static fs::path data_file_path() {
  if (const char* local_app_data = std::getenv("LOCALAPPDATA")) {
    return fs::path(local_app_data) / "Campfire" / "games.tsv";
  }
  return fs::current_path() / "data" / "games.tsv";
}

// Root directories that `scan` inspects for installed games.
static const std::vector<fs::path>& scan_roots() {
  static const std::vector<fs::path> roots = {
      R"(C:\games)",
      R"(D:\games)",
      R"(C:\Program Files (x86)\Steam\steamapps\common)",
  };
  return roots;
}

// Lowercases text for case-insensitive comparisons.
static std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

// Returns true when a file path has an .exe extension.
static bool is_exe_file(const fs::path& path) {
  return path.has_extension() && to_lower(path.extension().string()) == ".exe";
}

// Filters executable names that are usually installers or non-launcher tools.
static bool is_unwanted_exe_name(const std::string& exe_name) {
  const std::string name = to_lower(exe_name);
  return name.find("unins") != std::string::npos ||
         name.find("uninstall") != std::string::npos ||
         name.find("launcher") != std::string::npos ||
         name.find("updater") != std::string::npos ||
         name.find("bootstrap") != std::string::npos ||
         name.find("setup") != std::string::npos ||
         name.find("install") != std::string::npos ||
         name.find("vc_redist") != std::string::npos ||
         name.find("crash") != std::string::npos;
}

// Scores an executable candidate so we can select the most likely launcher binary.
static int score_candidate_exe(const fs::path& exe_path, const std::string& game_dir_name) {
  const std::string stem = to_lower(exe_path.stem().string());
  const std::string game = to_lower(game_dir_name);

  int score = 0;
  if (stem == game) {
    score += 100;
  }
  if (stem.find(game) != std::string::npos || game.find(stem) != std::string::npos) {
    score += 40;
  }
  if (!is_unwanted_exe_name(exe_path.filename().string())) {
    score += 20;
  }

  // Larger binaries are slightly preferred to avoid tiny helper executables.
  std::error_code ec;
  const auto size = fs::file_size(exe_path, ec);
  if (!ec) {
    score += static_cast<int>(std::min<std::uintmax_t>(size / (1024 * 1024), 200));
  }
  return score;
}

// Recursively scans one game directory and returns the best executable candidate.
static fs::path pick_game_executable(const fs::path& game_dir) {
  fs::path best_path;
  int best_score = -1;

  std::error_code ec;
  fs::recursive_directory_iterator it(
      game_dir,
      fs::directory_options::skip_permission_denied,
      ec);
  if (ec) {
    return {};
  }

  for (const auto& entry : it) {
    if (!entry.is_regular_file()) {
      continue;
    }

    const fs::path exe_path = entry.path();
    if (!is_exe_file(exe_path)) {
      continue;
    }

    const int score = score_candidate_exe(exe_path, game_dir.filename().string());
    if (score > best_score) {
      best_score = score;
      best_path = exe_path;
    }
  }

  return best_path;
}

// Ensures storage folder and file exist before reads/writes.
static void ensure_data_file() {
  const fs::path file_path = data_file_path();
  fs::create_directories(file_path.parent_path());
  if (!fs::exists(file_path)) {
    std::ofstream out(file_path);
    if (!out) {
      throw std::runtime_error("failed to create data file");
    }
  }
}

// Splits one TSV line into fields and preserves an empty trailing field.
static std::vector<std::string> split_tab_line(const std::string& line) {
  std::vector<std::string> parts;
  std::stringstream ss(line);
  std::string item;

  while (std::getline(ss, item, '\t')) {
    parts.push_back(item);
  }

  if (!line.empty() && line.back() == '\t') {
    parts.push_back("");
  }

  return parts;
}

static long long parse_non_negative_int64(const std::string& value, long long default_value) {
  try {
    const long long parsed = std::stoll(value);
    return parsed < 0 ? default_value : parsed;
  } catch (...) {
    return default_value;
  }
}

static std::vector<std::string> split_pipe_line(const std::string& line) {
  std::vector<std::string> parts;
  std::stringstream ss(line);
  std::string item;
  while (std::getline(ss, item, '|')) {
    if (!item.empty()) {
      parts.push_back(item);
    }
  }
  return parts;
}

static std::string join_pipe_line(const std::vector<std::string>& parts) {
  std::string out;
  bool first = true;
  for (const std::string& part : parts) {
    if (part.empty()) {
      continue;
    }
    if (!first) {
      out += '|';
    }
    out += part;
    first = false;
  }
  return out;
}

static std::vector<std::string> normalize_candidates_for_storage(
    const std::string& primary_exe,
    const std::vector<std::string>& candidates) {
  std::vector<std::string> normalized;
  std::unordered_set<std::string> seen;

  const auto add_unique = [&](const std::string& path) {
    if (path.empty()) {
      return;
    }
    const std::string key = to_lower(path);
    if (seen.find(key) != seen.end()) {
      return;
    }
    seen.insert(key);
    normalized.push_back(path);
  };

  add_unique(primary_exe);
  for (const std::string& path : candidates) {
    add_unique(path);
  }

  if (normalized.size() > 3) {
    normalized.resize(3);
  }
  return normalized;
}

std::vector<Game> load_games() {
  ensure_data_file();

  std::ifstream in(data_file_path());
  if (!in) {
    throw std::runtime_error("failed to open data file");
  }

  std::vector<Game> games;
  std::string line;

  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }

    const std::vector<std::string> parts = split_tab_line(line);
    if (parts.size() < 4) {
      continue;
    }

    const long long play_count = parts.size() >= 5 ? parse_non_negative_int64(parts[4], 0) : 0;
    const long long total_play_seconds = parts.size() >= 6 ? parse_non_negative_int64(parts[5], 0) : 0;
    const std::vector<std::string> parsed_candidates =
        parts.size() >= 7 ? split_pipe_line(parts[6]) : std::vector<std::string>{};
    const std::vector<std::string> exe_candidates =
        normalize_candidates_for_storage(parts[2], parsed_candidates);

    games.push_back(Game{parts[0], parts[1], parts[2], exe_candidates, parts[3], play_count, total_play_seconds});
  }

  return games;
}

// Parses either legacy IDs (`g_001`) or numeric IDs (`1`).
static int parse_id_number(const std::string& id) {
  if (id.rfind("g_", 0) == 0) {
    return std::stoi(id.substr(2));
  }
  return std::stoi(id);
}

// Persists the full game list to TSV.
static void save_games(const std::vector<Game>& games) {
  std::ofstream out(data_file_path(), std::ios::trunc);
  if (!out) {
    throw std::runtime_error("failed to write data file");
  }

  for (const Game& game : games) {
    const std::vector<std::string> exe_candidates =
        normalize_candidates_for_storage(game.exe_path, game.exe_candidates);

    out << game.id << '\t'
        << game.name << '\t'
        << game.exe_path << '\t'
        << game.args << '\t'
        << game.play_count << '\t'
        << game.total_play_seconds << '\t'
        << join_pipe_line(exe_candidates) << '\n';
  }
}

Game add_game(const std::string& name, const std::string& exe_path, const std::string& args) {
  std::vector<Game> games = load_games();

  int max_id = 0;
  for (const Game& game : games) {
    try {
      const int n = parse_id_number(game.id);
      if (n > max_id) {
        max_id = n;
      }
    } catch (...) {
      // Ignore malformed IDs to keep reading otherwise valid records.
    }
  }

  Game game{std::to_string(max_id + 1), name, exe_path, {exe_path}, args, 0, 0};
  games.push_back(game);
  save_games(games);
  return game;
}

bool remove_game(const std::string& id) {
  std::vector<Game> games = load_games();
  const auto old_size = games.size();

  games.erase(
      std::remove_if(games.begin(), games.end(), [&](const Game& game) {
        return game.id == id;
      }),
      games.end());

  if (games.size() == old_size) {
    return false;
  }

  save_games(games);
  return true;
}

bool update_game(const Game& updated) {
  std::vector<Game> games = load_games();
  bool found = false;

  for (Game& game : games) {
    if (game.id == updated.id) {
      game = updated;
      found = true;
      break;
    }
  }

  if (!found) {
    return false;
  }

  save_games(games);
  return true;
}

std::vector<ScannedGame> scan_common_game_dirs() {
  std::vector<ScannedGame> found;
  std::unordered_set<std::string> seen_exe_paths;

  for (const fs::path& root : scan_roots()) {
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
      continue;
    }

    fs::directory_iterator dir_it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
      continue;
    }

    for (const auto& entry : dir_it) {
      if (!entry.is_directory()) {
        continue;
      }

      const fs::path game_dir = entry.path();
      const fs::path exe_path = pick_game_executable(game_dir);
      if (exe_path.empty()) {
        continue;
      }

      const std::string key = to_lower(exe_path.string());
      if (seen_exe_paths.find(key) != seen_exe_paths.end()) {
        continue;
      }

      found.push_back(ScannedGame{game_dir.filename().string(), exe_path.string()});
      seen_exe_paths.insert(key);
    }
  }

  return found;
}

std::string game_library_file_path() {
  return data_file_path().string();
}

std::vector<std::string> common_scan_roots() {
  std::vector<std::string> roots;
  roots.reserve(scan_roots().size());
  for (const fs::path& root : scan_roots()) {
    roots.push_back(root.string());
  }
  return roots;
}
