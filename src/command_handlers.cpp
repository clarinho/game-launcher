#include "command_handlers.hpp"

#include "cli_args.hpp"
#include "console_ui.hpp"
#include "library.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace fs = std::filesystem;

namespace {
constexpr long long kForceFallbackMinSessionSeconds = 10;

struct LaunchResult {
  int code = 1;
  long long session_seconds = 0;
};

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

bool has_flag(const std::vector<std::string>& args, const std::string& flag) {
  for (const std::string& arg : args) {
    if (arg == flag) {
      return true;
    }
  }
  return false;
}

bool is_all_digits(const std::string& value) {
  if (value.empty()) {
    return false;
  }
  for (char ch : value) {
    if (ch < '0' || ch > '9') {
      return false;
    }
  }
  return true;
}

std::vector<std::string> extract_quoted_tokens(const std::string& line) {
  std::vector<std::string> tokens;
  size_t pos = 0;
  while (pos < line.size()) {
    const size_t start = line.find('"', pos);
    if (start == std::string::npos) {
      break;
    }
    const size_t end = line.find('"', start + 1);
    if (end == std::string::npos) {
      break;
    }
    tokens.push_back(line.substr(start + 1, end - start - 1));
    pos = end + 1;
  }
  return tokens;
}

std::string detect_steam_app_id(const std::string& exe_path_text);

std::string now_iso_local() {
  const std::time_t now = std::time(nullptr);
  std::tm local_tm{};
#ifdef _WIN32
  localtime_s(&local_tm, &now);
#else
  localtime_r(&now, &local_tm);
#endif
  char buffer[32]{};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local_tm);
  return buffer;
}

fs::path campfire_data_dir() {
  if (const char* local_app_data = std::getenv("LOCALAPPDATA")) {
    return fs::path(local_app_data) / "Campfire";
  }
  return fs::current_path() / "data";
}

fs::path launch_log_path() {
  return campfire_data_dir() / "logs" / "launch.log";
}

void append_launch_log(const std::string& message) {
  std::error_code ec;
  const fs::path path = launch_log_path();
  fs::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::app);
  if (!out) {
    return;
  }
  out << "[" << now_iso_local() << "] " << message << '\n';
}

std::string detect_launcher_from_path(const std::string& exe_path_text) {
  std::string lower = ui::to_lower(exe_path_text);
  std::replace(lower.begin(), lower.end(), '/', '\\');

  if (!detect_steam_app_id(exe_path_text).empty() || lower.find("\\steamapps\\common\\") != std::string::npos) {
    return "Steam";
  }
  if (lower.find("\\riot games\\") != std::string::npos || lower.find("\\riot client\\") != std::string::npos) {
    return "Riot";
  }
  if (lower.find("\\xboxgames\\") != std::string::npos || lower.find("\\windowsapps\\") != std::string::npos) {
    return "Xbox";
  }
  if (lower.find("battle.net") != std::string::npos || lower.find("\\blizzard\\") != std::string::npos ||
      lower.find("\\blizzard app\\") != std::string::npos) {
    return "Battle.net";
  }
  if (lower.find("\\epic games\\") != std::string::npos || lower.find("\\epicgames\\") != std::string::npos) {
    return "Epic";
  }
  if (lower.find("\\ea games\\") != std::string::npos || lower.find("\\electronic arts\\") != std::string::npos ||
      lower.find("\\ea app\\") != std::string::npos) {
    return "EA";
  }
  return "Local";
}

std::string trim_copy(const std::string& value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
    ++start;
  }

  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }

  return value.substr(start, end - start);
}

std::vector<fs::path> possible_steam_roots_from_game_path(const std::string& exe_path_text) {
  std::vector<fs::path> roots;

  if (const char* pf86 = std::getenv("ProgramFiles(x86)")) {
    roots.push_back(fs::path(pf86) / "Steam");
  }

  const std::string lower_path = ui::to_lower(exe_path_text);
  const std::string token = "\\steamapps\\common\\";
  const size_t token_pos = lower_path.find(token);
  if (token_pos != std::string::npos) {
    roots.push_back(fs::path(exe_path_text.substr(0, token_pos)));
  }

  std::vector<fs::path> deduped;
  std::unordered_set<std::string> seen;
  for (const fs::path& root : roots) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(root, ec);
    const std::string key = ui::to_lower((ec ? root : canonical).string());
    if (seen.find(key) != seen.end()) {
      continue;
    }
    seen.insert(key);
    deduped.push_back(ec ? root : canonical);
  }
  return deduped;
}

void merge_steam_local_playtime_file(
    const fs::path& vdf_path,
    std::unordered_map<std::string, long long>& app_seconds) {
  std::ifstream in(vdf_path);
  if (!in) {
    return;
  }

  std::vector<std::string> section_stack;
  std::string pending_section;
  std::string line;

  while (std::getline(in, line)) {
    const std::string trimmed = ui::to_lower(trim_copy(line));

    if (trimmed.empty()) {
      continue;
    }
    if (trimmed == "{") {
      if (!pending_section.empty()) {
        section_stack.push_back(ui::to_lower(pending_section));
        pending_section.clear();
      }
      continue;
    }
    if (trimmed == "}") {
      pending_section.clear();
      if (!section_stack.empty()) {
        section_stack.pop_back();
      }
      continue;
    }

    const std::vector<std::string> tokens = extract_quoted_tokens(line);
    if (tokens.size() == 1) {
      pending_section = tokens[0];
      continue;
    }
    if (tokens.size() < 2) {
      continue;
    }

    pending_section.clear();
    if (section_stack.size() < 2) {
      continue;
    }

    const std::string current_section = section_stack.back();
    const std::string parent_section = section_stack[section_stack.size() - 2];
    const std::string key = ui::to_lower(tokens[0]);
    if (parent_section != "apps" || key != "playtime") {
      continue;
    }

    if (!is_all_digits(current_section) || !is_all_digits(tokens[1])) {
      continue;
    }

    long long minutes = 0;
    try {
      minutes = std::stoll(tokens[1]);
    } catch (...) {
      continue;
    }
    const long long seconds = minutes * 60;
    auto it = app_seconds.find(current_section);
    if (it == app_seconds.end() || seconds > it->second) {
      app_seconds[current_section] = seconds;
    }
  }
}

std::unordered_map<std::string, long long> load_local_steam_play_seconds(
    const std::vector<fs::path>& steam_roots) {
  std::unordered_map<std::string, long long> app_seconds;

  for (const fs::path& steam_root : steam_roots) {
    const fs::path userdata_dir = steam_root / "userdata";
    std::error_code ec;
    if (!fs::exists(userdata_dir, ec) || ec || !fs::is_directory(userdata_dir, ec)) {
      continue;
    }

    fs::directory_iterator user_it(userdata_dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
      continue;
    }
    for (const auto& user_entry : user_it) {
      if (!user_entry.is_directory()) {
        continue;
      }
      const fs::path localconfig = user_entry.path() / "config" / "localconfig.vdf";
      if (!fs::exists(localconfig, ec) || ec || !fs::is_regular_file(localconfig, ec)) {
        continue;
      }
      merge_steam_local_playtime_file(localconfig, app_seconds);
    }
  }

  return app_seconds;
}

bool sync_steam_playtime_from_local_cache(
    Game& game,
    const std::unordered_map<std::string, long long>& steam_play_seconds) {
  const std::string app_id = detect_steam_app_id(game.exe_path);
  if (app_id.empty()) {
    return false;
  }
  const auto it = steam_play_seconds.find(app_id);
  if (it == steam_play_seconds.end()) {
    return false;
  }
  if (game.total_play_seconds == it->second) {
    return false;
  }

  game.total_play_seconds = it->second;
  return true;
}

std::vector<fs::path> collect_steam_roots_for_games(const std::vector<Game>& games) {
  std::vector<fs::path> roots;
  std::unordered_set<std::string> seen;

  for (const Game& game : games) {
    for (const fs::path& root : possible_steam_roots_from_game_path(game.exe_path)) {
      const std::string key = ui::to_lower(root.string());
      if (seen.find(key) != seen.end()) {
        continue;
      }
      seen.insert(key);
      roots.push_back(root);
    }
  }

  if (roots.empty()) {
    roots = possible_steam_roots_from_game_path("");
  }

  return roots;
}

std::string read_steam_appid_txt(const fs::path& dir) {
  const fs::path appid_file = dir / "steam_appid.txt";
  std::error_code ec;
  if (!fs::exists(appid_file, ec) || ec || !fs::is_regular_file(appid_file, ec)) {
    return "";
  }

  std::ifstream in(appid_file);
  if (!in) {
    return "";
  }

  std::string line;
  if (!std::getline(in, line)) {
    return "";
  }
  return is_all_digits(line) ? line : "";
}

bool is_path_in_steam_common(const fs::path& path) {
  std::string lower_path = ui::to_lower(path.string());
  std::replace(lower_path.begin(), lower_path.end(), '/', '\\');
  return lower_path.find("\\steamapps\\common\\") != std::string::npos;
}

std::string detect_steam_app_id_from_manifest(const fs::path& exe_path) {
  std::string lower_path = ui::to_lower(exe_path.string());
  std::replace(lower_path.begin(), lower_path.end(), '/', '\\');
  const std::string token = "\\steamapps\\common\\";
  const size_t token_pos = lower_path.find(token);
  if (token_pos == std::string::npos) {
    return "";
  }

  const fs::path root_prefix = fs::path(exe_path.string().substr(0, token_pos));
  const fs::path steamapps_dir = root_prefix / "steamapps";
  const fs::path common_dir = steamapps_dir / "common";

  std::error_code ec;
  const fs::path relative = fs::relative(exe_path, common_dir, ec);
  if (ec || relative.empty()) {
    return "";
  }

  auto it = relative.begin();
  if (it == relative.end()) {
    return "";
  }
  const std::string install_dir = ui::to_lower(it->string());

  fs::directory_iterator manifest_it(steamapps_dir, fs::directory_options::skip_permission_denied, ec);
  if (ec) {
    return "";
  }

  for (const auto& entry : manifest_it) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string file_name = entry.path().filename().string();
    if (file_name.rfind("appmanifest_", 0) != 0 || entry.path().extension() != ".acf") {
      continue;
    }

    std::ifstream manifest(entry.path());
    if (!manifest) {
      continue;
    }

    std::string line;
    while (std::getline(manifest, line)) {
      const std::vector<std::string> tokens = extract_quoted_tokens(line);
      if (tokens.size() >= 2 && ui::to_lower(tokens[0]) == "installdir" &&
          ui::to_lower(tokens[1]) == install_dir) {
        const size_t underscore = file_name.find('_');
        const size_t dot = file_name.rfind('.');
        if (underscore != std::string::npos && dot != std::string::npos && dot > underscore + 1) {
          const std::string app_id = file_name.substr(underscore + 1, dot - underscore - 1);
          if (is_all_digits(app_id)) {
            return app_id;
          }
        }
      }
    }
  }

  return "";
}

std::string detect_steam_app_id(const std::string& exe_path_text) {
  std::error_code ec;
  fs::path exe_path = fs::weakly_canonical(fs::path(exe_path_text), ec);
  if (ec) {
    exe_path = fs::path(exe_path_text);
  }

  // Treat games as Steam only when their executable actually resides in a Steam library.
  if (!is_path_in_steam_common(exe_path)) {
    return "";
  }

  fs::path dir = exe_path.parent_path();
  for (int i = 0; i < 8 && !dir.empty(); ++i) {
    const std::string app_id = read_steam_appid_txt(dir);
    if (!app_id.empty()) {
      return app_id;
    }
    dir = dir.parent_path();
  }

  return detect_steam_app_id_from_manifest(exe_path);
}

std::string simplify_for_match(std::string value) {
  std::string out;
  out.reserve(value.size());
  for (char ch : value) {
    if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
      out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
  }
  return out;
}

bool is_unwanted_exe_name(const std::string& exe_name) {
  const std::string name = ui::to_lower(exe_name);
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

int score_candidate_exe(const fs::path& exe_path, const std::string& game_name) {
  const std::string stem = simplify_for_match(exe_path.stem().string());
  const std::string game = simplify_for_match(game_name);

  int score = 0;
  if (!stem.empty() && !game.empty() && stem == game) {
    score += 180;
  }
  if (!stem.empty() && !game.empty() &&
      (stem.find(game) != std::string::npos || game.find(stem) != std::string::npos)) {
    score += 70;
  }

  if (is_unwanted_exe_name(exe_path.filename().string())) {
    score -= 140;
  } else {
    score += 30;
  }

  std::error_code ec;
  const auto size = fs::file_size(exe_path, ec);
  if (!ec) {
    score += static_cast<int>(std::min<std::uintmax_t>(size / (1024 * 1024), 120));
  }
  return score;
}

std::vector<std::string> dedupe_paths_preserve_order(const std::vector<std::string>& paths) {
  std::vector<std::string> deduped;
  std::unordered_set<std::string> seen;
  deduped.reserve(paths.size());

  for (const std::string& path : paths) {
    if (path.empty()) {
      continue;
    }
    const std::string key = normalize_path_for_compare(path);
    if (seen.find(key) != seen.end()) {
      continue;
    }
    seen.insert(key);
    deduped.push_back(path);
  }

  return deduped;
}

std::vector<std::string> discover_exe_candidates_near_primary(
    const std::string& primary_exe,
    const std::string& game_name,
    size_t limit) {
  std::vector<std::pair<int, std::string>> scored;

  std::error_code ec;
  fs::path root = fs::path(primary_exe).parent_path();
  if (root.empty() || !fs::exists(root, ec) || ec || !fs::is_directory(root, ec)) {
    return {};
  }

  fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
  if (ec) {
    return {};
  }

  for (const auto& entry : it) {
    if (it.depth() > 3) {
      it.disable_recursion_pending();
      continue;
    }
    if (!entry.is_regular_file()) {
      continue;
    }

    const fs::path exe_path = entry.path();
    if (ui::to_lower(exe_path.extension().string()) != ".exe") {
      continue;
    }

    scored.push_back({score_candidate_exe(exe_path, game_name), exe_path.string()});
  }

  std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
    if (a.first != b.first) {
      return a.first > b.first;
    }
    return ui::to_lower(a.second) < ui::to_lower(b.second);
  });

  std::vector<std::string> out;
  out.reserve(limit);
  for (const auto& item : scored) {
    out.push_back(item.second);
    if (out.size() >= limit) {
      break;
    }
  }
  return out;
}

std::vector<std::string> build_launch_candidates(const Game& game, size_t limit = 3) {
  std::vector<std::string> merged;
  merged.reserve(limit + 4);

  merged.push_back(game.exe_path);
  for (const std::string& path : game.exe_candidates) {
    merged.push_back(path);
  }

  std::vector<std::string> deduped = dedupe_paths_preserve_order(merged);
  if (deduped.size() < limit) {
    const std::vector<std::string> discovered =
        discover_exe_candidates_near_primary(game.exe_path, game.name, limit * 2);
    deduped.insert(deduped.end(), discovered.begin(), discovered.end());
    deduped = dedupe_paths_preserve_order(deduped);
  }

  if (deduped.size() > limit) {
    deduped.resize(limit);
  }
  return deduped;
}

std::vector<std::string> reorder_candidates_after_success(
    const std::vector<std::string>& candidates,
    size_t success_index) {
  if (candidates.empty() || success_index >= candidates.size() || success_index == 0) {
    return candidates;
  }

  std::vector<std::string> reordered = candidates;
  const std::string original_primary = reordered.front();
  const std::string winner = reordered[success_index];

  reordered.erase(reordered.begin() + static_cast<std::ptrdiff_t>(success_index));
  reordered.erase(reordered.begin());
  reordered.insert(reordered.begin(), winner);

  auto old_primary_it = std::find(reordered.begin() + 1, reordered.end(), original_primary);
  if (old_primary_it != reordered.end()) {
    reordered.erase(old_primary_it);
  }

  const size_t target_index = std::min<size_t>(2, reordered.size());
  reordered.insert(reordered.begin() + static_cast<std::ptrdiff_t>(target_index), original_primary);

  if (reordered.size() > 3) {
    reordered.resize(3);
  }
  return reordered;
}

std::string format_duration(long long total_seconds) {
  const double total_hours = static_cast<double>(total_seconds) / 3600.0;
  std::ostringstream out;
  out << std::fixed << std::setprecision(1) << total_hours << "h";
  return out.str();
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
std::vector<DWORD> list_process_ids_by_exe_name(const std::string& exe_name_lower) {
  std::vector<DWORD> pids;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return pids;
  }

  PROCESSENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  if (Process32First(snapshot, &entry)) {
    do {
      const std::string process_name = ui::to_lower(entry.szExeFile);
      if (process_name == exe_name_lower) {
        pids.push_back(entry.th32ProcessID);
      }
    } while (Process32Next(snapshot, &entry));
  }

  CloseHandle(snapshot);
  return pids;
}

bool contains_pid(const std::vector<DWORD>& pids, DWORD pid) {
  return std::find(pids.begin(), pids.end(), pid) != pids.end();
}

LaunchResult start_steam_process(const std::string& app_id, const std::string& expected_exe_path) {
  std::string steam_exe = "steam.exe";
  std::string steam_workdir;
  const std::string expected_exe_name = ui::to_lower(fs::path(expected_exe_path).filename().string());
  const std::vector<DWORD> existing_pids = list_process_ids_by_exe_name(expected_exe_name);

  if (const char* pf86 = std::getenv("ProgramFiles(x86)")) {
    const fs::path candidate = fs::path(pf86) / "Steam" / "steam.exe";
    std::error_code ec;
    if (fs::exists(candidate, ec) && !ec) {
      steam_exe = candidate.string();
      steam_workdir = candidate.parent_path().string();
    }
  }

  std::string command_line = "\"" + steam_exe + "\" -applaunch " + app_id;
  std::vector<char> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back('\0');

  STARTUPINFOA startup_info{};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info{};

  const BOOL ok = CreateProcessA(
      nullptr,
      mutable_command.data(),
      nullptr,
      nullptr,
      FALSE,
      0,
      nullptr,
      steam_workdir.empty() ? nullptr : steam_workdir.c_str(),
      &startup_info,
      &process_info);

  if (!ok) {
    ui::print_error("Failed to launch via Steam. Windows error: " + std::to_string(GetLastError()));
    return {};
  }

  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);

  const auto wait_start = std::chrono::steady_clock::now();
  HANDLE game_process = nullptr;
  for (int i = 0; i < 240; ++i) {
    const std::vector<DWORD> current_pids = list_process_ids_by_exe_name(expected_exe_name);
    for (DWORD pid : current_pids) {
      if (contains_pid(existing_pids, pid)) {
        continue;
      }
      game_process = OpenProcess(SYNCHRONIZE, FALSE, pid);
      if (game_process != nullptr) {
        break;
      }
    }
    if (game_process != nullptr) {
      break;
    }
    Sleep(500);
  }

  if (game_process == nullptr) {
    ui::print_info("Steam launch started, but game process tracking was unavailable.");
    return LaunchResult{0, 0};
  }

  const DWORD wait_result = WaitForSingleObject(game_process, INFINITE);
  CloseHandle(game_process);
  if (wait_result != WAIT_OBJECT_0) {
    ui::print_info("Steam launch started, but session timing could not be completed.");
    return LaunchResult{0, 0};
  }

  const auto wait_end = std::chrono::steady_clock::now();
  const auto elapsed_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(wait_end - wait_start).count();
  return LaunchResult{0, elapsed_seconds};
}

LaunchResult start_process(const std::string& exe_path, const std::string& launch_args) {
  std::string command_line = "\"" + exe_path + "\"";
  if (!launch_args.empty()) {
    command_line += " " + launch_args;
  }
  const std::string working_dir = fs::path(exe_path).parent_path().string();

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
      working_dir.empty() ? nullptr : working_dir.c_str(),
      &startup_info,
      &process_info);

  if (!ok) {
    const DWORD error = GetLastError();
    if (error == ERROR_ELEVATION_REQUIRED) {
      ui::print_error("Failed to launch game. This executable requires Administrator privileges.");
    } else {
      ui::print_error("Failed to launch game. Windows error: " + std::to_string(error));
    }
    return {};
  }

  const auto session_start = std::chrono::steady_clock::now();
  const DWORD wait_result = WaitForSingleObject(process_info.hProcess, INFINITE);
  if (wait_result != WAIT_OBJECT_0) {
    ui::print_error("Failed while waiting for game process to exit.");
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return {};
  }
  const auto session_end = std::chrono::steady_clock::now();
  const auto elapsed_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(session_end - session_start).count();

  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  return LaunchResult{0, elapsed_seconds};
}

bool is_process_running_for_path(const std::string& exe_path) {
  const std::string exe_name = ui::to_lower(fs::path(exe_path).filename().string());
  return !list_process_ids_by_exe_name(exe_name).empty();
}
#else
LaunchResult start_steam_process(const std::string& app_id, const std::string& expected_exe_path) {
  (void)app_id;
  (void)expected_exe_path;
  ui::print_error("Steam launch mode is not supported on this platform yet.");
  return {};
}

LaunchResult start_process(const std::string& exe_path, const std::string& launch_args) {
  std::string command = "\"" + exe_path + "\"";
  if (!launch_args.empty()) {
    command += " " + launch_args;
  }
  const auto session_start = std::chrono::steady_clock::now();
  if (std::system(command.c_str()) != 0) {
    ui::print_error("Failed to launch game process.");
    return {};
  }
  const auto session_end = std::chrono::steady_clock::now();
  const auto elapsed_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(session_end - session_start).count();
  return LaunchResult{0, elapsed_seconds};
}

bool is_process_running_for_path(const std::string& exe_path) {
  (void)exe_path;
  return false;
}
#endif

bool write_doctor_report(std::ostream& out) {
  bool healthy = true;
  out << "[info] Running Campfire diagnostics...\n";

  const char* local_app_data = std::getenv("LOCALAPPDATA");
  if (local_app_data != nullptr && local_app_data[0] != '\0') {
    out << "[ok] LOCALAPPDATA: " << local_app_data << '\n';
  } else {
    out << "[info] LOCALAPPDATA is not set. Campfire will use a local ./data fallback.\n";
  }

  const std::string library_path = game_library_file_path();
  out << "[info] Library file: " << library_path << '\n';

  std::vector<Game> games;
  try {
    games = load_games();
    out << "[ok] Library is readable. Entries: " << games.size() << '\n';
  } catch (const std::exception& ex) {
    out << "[error] Library read failed: " << ex.what() << '\n';
    return false;
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
    out << "[ok] No duplicate executable paths in library.\n";
  } else {
    out << "[error] Duplicate executable entries detected: " << duplicate_exe_count << '\n';
    healthy = false;
  }

  if (missing_exe_count == 0) {
    out << "[ok] All library executables exist on disk.\n";
  } else {
    out << "[error] Missing executable files: " << missing_exe_count << '\n';
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
  out << "[info] Scan roots present: " << present_root_count << "/" << roots.size() << '\n';

  if (healthy) {
    out << "[ok] Doctor checks passed.\n";
  } else {
    out << "[error] Doctor found issues.\n";
  }
  return healthy;
}
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
  std::vector<Game> games = load_games();
  if (games.empty()) {
    ui::print_info("No games yet. Add one with `campfire add ...`");
    return 0;
  }

  const std::vector<fs::path> steam_roots = collect_steam_roots_for_games(games);
  const std::unordered_map<std::string, long long> steam_play_seconds =
      load_local_steam_play_seconds(steam_roots);
  for (Game& game : games) {
    if (sync_steam_playtime_from_local_cache(game, steam_play_seconds)) {
      update_game(game);
    }
  }

  ui::print_list_table(games);
  return 0;
}

// Prints details and stats for one game entry.
int handle_info(const std::vector<std::string>& args) {
  const std::string id = get_arg_value(args, "--id");
  const std::string name = get_arg_value(args, "--name");
  if ((id.empty() && name.empty()) || (!id.empty() && !name.empty())) {
    ui::print_error("info requires exactly one of --id or --name");
    ui::print_info("Example: campfire info --id 25");
    ui::print_info("Example: campfire info --name \"It Takes Two\"");
    return 1;
  }

  std::vector<Game> games = load_games();
  Game* game = nullptr;
  if (!id.empty()) {
    for (Game& candidate : games) {
      if (candidate.id == id) {
        game = &candidate;
        break;
      }
    }
  } else {
    const std::string wanted = ui::to_lower(name);
    for (Game& candidate : games) {
      if (ui::to_lower(candidate.name) == wanted) {
        game = &candidate;
        break;
      }
    }
  }

  if (game == nullptr) {
    ui::print_error("No game found for info target.");
    return 1;
  }

  const std::unordered_map<std::string, long long> steam_play_seconds =
      load_local_steam_play_seconds(possible_steam_roots_from_game_path(game->exe_path));
  if (sync_steam_playtime_from_local_cache(*game, steam_play_seconds)) {
    update_game(*game);
  }

  std::cout << "id: " << game->id << '\n';
  std::cout << "name: " << game->name << '\n';
  std::cout << "path: " << game->exe_path << '\n';
  const std::string launcher = detect_launcher_from_path(game->exe_path);
  std::cout << "launcher: " << launcher << '\n';
  const std::string steam_app_id = detect_steam_app_id(game->exe_path);
  if (!steam_app_id.empty()) {
    std::cout << "steam app id: " << steam_app_id << '\n';
  }
  if (!game->exe_candidates.empty()) {
    std::cout << "launch candidates: ";
    for (size_t i = 0; i < game->exe_candidates.size(); ++i) {
      if (i > 0) {
        std::cout << " | ";
      }
      std::cout << game->exe_candidates[i];
    }
    std::cout << '\n';
  }
  std::cout << "time played: " << format_duration(game->total_play_seconds) << '\n';
  std::cout << "play count: " << game->play_count << '\n';
  return 0;
}

// Parses and validates `edit` flags, then updates one existing library entry.
int handle_edit(const std::vector<std::string>& args) {
  const auto print_edit_examples = []() {
    ui::print_info("Example (one field): campfire edit --id 25 --name \"It Takes Two\"");
    ui::print_info(
        "Example (all fields): campfire edit --id 25 --name \"It Takes Two\" --exe \"C:\\Games\\ItTakesTwo\\ItTakesTwo.exe\" --args \"-windowed\"");
  };

  const std::string id = get_arg_value(args, "--id");
  if (id.empty()) {
    ui::print_error("edit requires --id");
    print_edit_examples();
    return 1;
  }

  const bool has_name_flag = has_flag(args, "--name");
  const bool has_exe_flag = has_flag(args, "--exe");
  const bool has_args_flag = has_flag(args, "--args");
  if (!has_name_flag && !has_exe_flag && !has_args_flag) {
    ui::print_error("edit requires at least one of --name, --exe, or --args");
    print_edit_examples();
    return 1;
  }

  const std::string new_name = get_arg_value(args, "--name");
  const std::string new_exe_raw = get_arg_value(args, "--exe");
  const std::string new_args = get_arg_value(args, "--args");

  if (has_name_flag && new_name.empty()) {
    ui::print_error("edit --name requires a value");
    print_edit_examples();
    return 1;
  }
  if (has_exe_flag && new_exe_raw.empty()) {
    ui::print_error("edit --exe requires a value");
    print_edit_examples();
    return 1;
  }
  if (has_args_flag && new_args.empty()) {
    ui::print_error("edit --args requires a value");
    print_edit_examples();
    return 1;
  }

  std::vector<Game> games = load_games();
  Game* target = nullptr;
  for (Game& game : games) {
    if (game.id == id) {
      target = &game;
      break;
    }
  }

  if (target == nullptr) {
    ui::print_error("No game found with id: " + id);
    print_edit_examples();
    return 1;
  }

  if (has_name_flag) {
    target->name = new_name;
  }

  if (has_exe_flag) {
    std::error_code ec;
    const fs::path exe_path(new_exe_raw);
    if (!fs::exists(exe_path, ec) || ec) {
      ui::print_error("Executable does not exist: " + new_exe_raw);
      print_edit_examples();
      return 1;
    }
    if (!fs::is_regular_file(exe_path, ec) || ec) {
      ui::print_error("Executable path is not a file: " + new_exe_raw);
      print_edit_examples();
      return 1;
    }
    if (!is_exe_extension(exe_path)) {
      ui::print_error("Executable must be a .exe file: " + new_exe_raw);
      print_edit_examples();
      return 1;
    }

    const std::string normalized_exe = normalize_path_for_storage(new_exe_raw);
    const std::string normalized_key = normalize_path_for_compare(normalized_exe);
    for (const Game& game : games) {
      if (game.id == id) {
        continue;
      }
      if (normalize_path_for_compare(game.exe_path) == normalized_key) {
        ui::print_error("A game with this executable already exists (id: " + game.id + ")");
        print_edit_examples();
        return 1;
      }
    }
    std::vector<std::string> merged_candidates;
    merged_candidates.push_back(normalized_exe);
    merged_candidates.push_back(target->exe_path);
    for (const std::string& candidate : target->exe_candidates) {
      merged_candidates.push_back(candidate);
    }
    merged_candidates = dedupe_paths_preserve_order(merged_candidates);
    if (merged_candidates.size() > 3) {
      merged_candidates.resize(3);
    }

    target->exe_path = normalized_exe;
    target->exe_candidates = merged_candidates;
  }

  if (has_args_flag) {
    target->args = new_args;
  }

  if (!update_game(*target)) {
    ui::print_error("Failed to update game with id: " + id);
    print_edit_examples();
    return 1;
  }

  ui::print_ok("Updated game: " + target->id);
  std::cout << "name: " << target->name << '\n';
  std::cout << "exe: " << target->exe_path << '\n';
  if (!target->args.empty()) {
    std::cout << "args: " << target->args << '\n';
  }
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
  std::ostringstream report;
  const bool healthy = write_doctor_report(report);
  std::cout << report.str();
  return healthy ? 0 : 1;
}

int handle_debug() {
  std::error_code ec;
  const fs::path debug_dir = campfire_data_dir() / "debug";
  fs::create_directories(debug_dir, ec);
  if (ec) {
    ui::print_error("Failed to create debug directory: " + debug_dir.string());
    return 1;
  }

  std::string stamp = now_iso_local();
  std::replace(stamp.begin(), stamp.end(), ' ', '_');
  std::replace(stamp.begin(), stamp.end(), ':', '-');
  const fs::path report_path = debug_dir / ("campfire-debug-" + stamp + ".txt");

  std::ofstream out(report_path, std::ios::trunc);
  if (!out) {
    ui::print_error("Failed to write debug report: " + report_path.string());
    return 1;
  }

  out << "Campfire Debug Report\n";
  out << "Generated: " << now_iso_local() << "\n\n";

  out << "=== Doctor ===\n";
  std::ostringstream doctor_text;
  const bool doctor_ok = write_doctor_report(doctor_text);
  out << doctor_text.str() << '\n';

  out << "=== Scan (detected) ===\n";
  const std::vector<ScannedGame> found = scan_common_game_dirs();
  if (found.empty()) {
    out << "(none)\n\n";
  } else {
    size_t width = 0;
    for (const ScannedGame& item : found) {
      width = std::max(width, item.name.size());
    }
    for (const ScannedGame& item : found) {
      out << std::left << std::setw(static_cast<int>(width)) << item.name
          << " | " << item.exe_path << '\n';
    }
    out << '\n';
  }

  out << "=== Launch Log ===\n";
  const fs::path log_path = launch_log_path();
  std::ifstream log_in(log_path);
  if (!log_in) {
    out << "(no launch log found at " << log_path.string() << ")\n";
  } else {
    out << log_in.rdbuf();
    out << '\n';
  }

  out.close();
  ui::print_ok("Debug report written: " + report_path.string());
  if (!doctor_ok) {
    ui::print_info("Doctor section found issues. See report for details.");
  }
  return 0;
}

// Launches one game by ID or exact name match.
int handle_launch(const std::vector<std::string>& args) {
  const std::string id = get_arg_value(args, "--id");
  const std::string name = get_arg_value(args, "--name");
  const bool force_fallback = has_flag(args, "--force-fallback");
  if ((id.empty() && name.empty()) || (!id.empty() && !name.empty())) {
    ui::print_error("launch requires exactly one of --id or --name");
    return 1;
  }

  const std::vector<Game> games = load_games();
  const Game* game = find_game_by_id_or_name(games, id, name);
  if (!game) {
    append_launch_log("launch failed: game not found for id/name target");
    ui::print_error("No game found for launch target.");
    return 1;
  }

  Game selected = *game;
  if (is_process_running_for_path(selected.exe_path)) {
    append_launch_log("launch skipped: already running | id=" + selected.id + " name=" + selected.name);
    ui::print_info("Game is already running: " + selected.name);
    return 0;
  }
  const std::unordered_map<std::string, long long> steam_play_seconds =
      load_local_steam_play_seconds(possible_steam_roots_from_game_path(selected.exe_path));
  if (sync_steam_playtime_from_local_cache(selected, steam_play_seconds)) {
    update_game(selected);
  }

  const std::vector<std::string> candidates = build_launch_candidates(selected);
  if (candidates.empty()) {
    append_launch_log("launch failed: no candidates | id=" + selected.id + " name=" + selected.name);
    ui::print_error("No executable candidates are configured for this game.");
    return 1;
  }

  LaunchResult launch_result;
  std::string launched_exe;
  std::string launched_steam_app_id;
  size_t successful_index = candidates.size();

  for (size_t i = 0; i < candidates.size(); ++i) {
    const std::string& candidate = candidates[i];
    std::error_code ec;
    if (!fs::exists(candidate, ec) || ec || !fs::is_regular_file(candidate, ec)) {
      append_launch_log(
          "candidate skipped missing [" + std::to_string(i + 1) + "] | id=" + selected.id + " exe=" + candidate);
      ui::print_info("Skipping missing candidate [" + std::to_string(i + 1) + "]: " + candidate);
      continue;
    }

    const std::string steam_app_id = detect_steam_app_id(candidate);
    if (!steam_app_id.empty()) {
      append_launch_log(
          "candidate attempt steam [" + std::to_string(i + 1) + "/" + std::to_string(candidates.size()) +
          "] | id=" + selected.id + " appid=" + steam_app_id + " exe=" + candidate);
      ui::print_info(
          "Trying candidate [" + std::to_string(i + 1) + "/" + std::to_string(candidates.size()) +
          "] via Steam app id: " + steam_app_id);
      launch_result = start_steam_process(steam_app_id, candidate);
    } else {
      append_launch_log(
          "candidate attempt native [" + std::to_string(i + 1) + "/" + std::to_string(candidates.size()) +
          "] | id=" + selected.id + " exe=" + candidate);
      ui::print_info(
          "Trying candidate [" + std::to_string(i + 1) + "/" + std::to_string(candidates.size()) +
          "]: " + candidate);
      launch_result = start_process(candidate, selected.args);
    }

    if (launch_result.code == 0) {
      if (force_fallback && steam_app_id.empty() &&
          launch_result.session_seconds < kForceFallbackMinSessionSeconds &&
          i + 1 < candidates.size()) {
        append_launch_log(
            "candidate suspicious short session " + std::to_string(launch_result.session_seconds) +
            "s | id=" + selected.id + " exe=" + candidate + " | trying fallback");
        ui::print_info(
            "Candidate exited after " + std::to_string(launch_result.session_seconds) +
            "s. Force fallback is enabled; trying next candidate.");
        continue;
      }

      launched_exe = candidate;
      launched_steam_app_id = steam_app_id;
      successful_index = i;
      break;
    }

    if (i + 1 < candidates.size()) {
      append_launch_log(
          "candidate failed [" + std::to_string(i + 1) + "/" + std::to_string(candidates.size()) +
          "] | id=" + selected.id + " exe=" + candidate);
      ui::print_info("Candidate failed. Trying next fallback...");
    }
  }

  if (launch_result.code != 0) {
    append_launch_log("launch failed all candidates | id=" + selected.id + " name=" + selected.name);
    ui::print_error("All launch candidates failed.");
    return launch_result.code;
  }

  if (launch_result.code == 0) {
    Game updated = selected;
    const std::vector<std::string> reordered = reorder_candidates_after_success(candidates, successful_index);
    if (!reordered.empty()) {
      updated.exe_path = reordered.front();
      updated.exe_candidates = reordered;
    }
    updated.play_count += 1;
    updated.total_play_seconds += launch_result.session_seconds;
    if (!update_game(updated)) {
      append_launch_log("launch succeeded but persist failed | id=" + selected.id + " exe=" + launched_exe);
      ui::print_error("Game launched, but failed to persist play stats.");
      return 1;
    }

    append_launch_log(
        "launch success | id=" + selected.id + " name=" + selected.name + " launcher=" +
        detect_launcher_from_path(launched_exe) + " session=" + std::to_string(launch_result.session_seconds) +
        "s total=" + std::to_string(updated.total_play_seconds) + "s exe=" + launched_exe);

    ui::print_ok("Launched: " + selected.name);
    if (!launched_steam_app_id.empty() && launch_result.session_seconds == 0) {
      std::cout << "session time: tracking unavailable for Steam launches\n";
    } else {
      std::cout << "session time: " << launch_result.session_seconds << "s\n";
    }
    std::cout << "exe used: " << launched_exe << '\n';
    if (successful_index > 0) {
      std::cout << "confidence updated: promoted fallback candidate to #1\n";
    }
    std::cout << "total time: " << updated.total_play_seconds << "s\n";
    std::cout << "play count: " << updated.play_count << '\n';
  }

  return launch_result.code;
}
}  // namespace commands
