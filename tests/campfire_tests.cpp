#include "cli_args.hpp"
#include "command_handlers.hpp"
#include "library.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
struct EnvGuard {
  explicit EnvGuard(const std::string& key) : key_(key) {
    const char* current = std::getenv(key_.c_str());
    if (current != nullptr) {
      had_value_ = true;
      original_ = current;
    }
  }

  ~EnvGuard() {
    if (had_value_) {
      set(original_);
    } else {
      clear();
    }
  }

  void set(const std::string& value) {
#ifdef _WIN32
    _putenv_s(key_.c_str(), value.c_str());
#else
    setenv(key_.c_str(), value.c_str(), 1);
#endif
  }

  void clear() {
#ifdef _WIN32
    _putenv_s(key_.c_str(), "");
#else
    unsetenv(key_.c_str());
#endif
  }

 private:
  std::string key_;
  std::string original_;
  bool had_value_ = false;
};

void assert_true(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}
}  // namespace

int main() {
  // cli_args round trip
  {
    char arg0[] = "campfire";
    char arg1[] = "add";
    char arg2[] = "--name";
    char arg3[] = "Hades";
    char* argv[] = {arg0, arg1, arg2, arg3};
    const auto args = collect_args(4, argv);
    assert_true(args.size() == 3, "collect_args should skip argv[0]");
    assert_true(args[0] == "add", "collect_args should preserve order");
  }

  {
    const std::vector<std::string> args = {"add", "--name", "Hades", "--exe", "C:\\Hades.exe"};
    assert_true(get_arg_value(args, "--name") == "Hades", "get_arg_value should parse existing flags");
    assert_true(get_arg_value(args, "--args").empty(), "get_arg_value should return empty for missing flags");
  }

  // library + handler behavior in isolated runtime directory
  EnvGuard local_app_data("LOCALAPPDATA");
  const fs::path temp_root = fs::temp_directory_path() / "campfire-tests-runtime";
  std::error_code ec;
  fs::remove_all(temp_root, ec);
  fs::create_directories(temp_root, ec);
  assert_true(!ec, "failed to create temp runtime directory");
  local_app_data.set(temp_root.string());
  const fs::path legacy_dir = temp_root / "Campfire";
  fs::create_directories(legacy_dir, ec);
  assert_true(!ec, "failed to create legacy test directory");
  const fs::path legacy_file = legacy_dir / "games.tsv";
  {
    std::ofstream legacy_out(legacy_file.string(), std::ios::trunc);
    legacy_out << "9\tLegacy Game\tC:\\Legacy\\game.exe\t-windowed\n";
  }
  {
    const auto legacy_games = load_games();
    assert_true(legacy_games.size() == 1, "legacy row should still load");
    assert_true(legacy_games[0].play_count == 0, "legacy row should default play_count");
    assert_true(legacy_games[0].total_play_seconds == 0, "legacy row should default total_play_seconds");
  }
  {
    std::ofstream clear_out(legacy_file.string(), std::ios::trunc);
  }

  const fs::path exe_dir = temp_root / "bin";
  fs::create_directories(exe_dir, ec);
  assert_true(!ec, "failed to create temp exe directory");
  const fs::path fake_exe = exe_dir / "game.exe";
  std::ofstream fake_exe_file(fake_exe.string(), std::ios::binary);
  fake_exe_file << "MZ";
  fake_exe_file.close();
  const fs::path fake_exe_2 = exe_dir / "game2.exe";
  std::ofstream fake_exe_file_2(fake_exe_2.string(), std::ios::binary);
  fake_exe_file_2 << "MZ";
  fake_exe_file_2.close();

  {
    const std::vector<std::string> add_args = {
        "add",
        "--name",
        "Test Game",
        "--exe",
        fake_exe.string(),
        "--args",
        "-windowed"};
    const int code = commands::handle_add(add_args);
    assert_true(code == 0, "first add should succeed");
  }

  {
    const auto games = load_games();
    assert_true(!games.empty(), "expected at least one game for info test");
    const std::vector<std::string> info_args = {"info", "--id", games[0].id};
    const int code = commands::handle_info(info_args);
    assert_true(code == 0, "info should succeed for valid id");
  }

  {
    const std::vector<std::string> info_args = {"info", "--id", "999999"};
    const int code = commands::handle_info(info_args);
    assert_true(code != 0, "info should fail for unknown id");
  }

  {
    const std::vector<std::string> add_args = {
        "add",
        "--name",
        "Duplicate Game",
        "--exe",
        fake_exe.string()};
    const int code = commands::handle_add(add_args);
    assert_true(code != 0, "duplicate add should be rejected");
  }

  {
    const int code = commands::handle_doctor();
    assert_true(code == 0, "doctor should pass for healthy test runtime");
  }

  {
    const auto games_before_edit = load_games();
    assert_true(games_before_edit.size() == 1, "expected one game before edit test");

    const std::vector<std::string> edit_args = {
        "edit",
        "--id",
        games_before_edit[0].id,
        "--name",
        "Renamed Game",
        "--exe",
        fake_exe_2.string(),
        "--args",
        "-dx11"};
    const int code = commands::handle_edit(edit_args);
    assert_true(code == 0, "edit should update existing game");

    const auto games_after_edit = load_games();
    assert_true(games_after_edit.size() == 1, "library size should remain unchanged after edit");
    assert_true(games_after_edit[0].name == "Renamed Game", "edit should update name");
    assert_true(
        games_after_edit[0].exe_path.find("game2.exe") != std::string::npos,
        "edit should update executable path");
    assert_true(games_after_edit[0].args == "-dx11", "edit should update args");
  }

  {
    const std::vector<std::string> add_args = {
        "add",
        "--name",
        "Second Game",
        "--exe",
        fake_exe.string()};
    const int code = commands::handle_add(add_args);
    assert_true(code == 0, "adding second unique game should succeed");
  }

  {
    const auto games = load_games();
    assert_true(games.size() == 2, "expected two games before duplicate edit test");

    const std::vector<std::string> edit_args = {
        "edit",
        "--id",
        games[1].id,
        "--exe",
        fake_exe_2.string()};
    const int code = commands::handle_edit(edit_args);
    assert_true(code != 0, "edit should reject duplicate executable path");
  }

  {
    const auto games = load_games();
    assert_true(games.size() == 2, "library should contain expected entries");
    assert_true(!games[0].id.empty(), "added game should have an id");
    const bool removed_first = remove_game(games[0].id);
    const bool removed_second = remove_game(games[1].id);
    assert_true(removed_first && removed_second, "remove_game should remove existing ids");
    assert_true(load_games().empty(), "library should be empty after remove");
  }

#ifdef _WIN32
  {
    const char* comspec = std::getenv("ComSpec");
    const std::string cmd_path =
        (comspec != nullptr && comspec[0] != '\0') ? std::string(comspec) : "C:\\Windows\\System32\\cmd.exe";

    const std::vector<std::string> add_args = {
        "add",
        "--name",
        "Launch Stats Game",
        "--exe",
        cmd_path,
        "--args",
        "/c exit 0"};
    const int add_code = commands::handle_add(add_args);
    assert_true(add_code == 0, "launch-stat game add should succeed");

    const auto games = load_games();
    std::string launch_id;
    for (const Game& game : games) {
      if (game.name == "Launch Stats Game") {
        launch_id = game.id;
        break;
      }
    }
    assert_true(!launch_id.empty(), "launch-stat game should be discoverable");

    const std::vector<std::string> launch_args = {"launch", "--id", launch_id};
    const int launch_code = commands::handle_launch(launch_args);
    assert_true(launch_code == 0, "launch should succeed for cmd.exe");

    const auto after_launch = load_games();
    bool verified = false;
    for (const Game& game : after_launch) {
      if (game.id == launch_id) {
        assert_true(game.play_count == 1, "play_count should increment after launch");
        assert_true(game.total_play_seconds >= 0, "total_play_seconds should be persisted");
        verified = true;
        break;
      }
    }
    assert_true(verified, "launched game stats should be verifiable");
    assert_true(remove_game(launch_id), "launch-stat game should be removable");
  }
#endif

  fs::remove_all(temp_root, ec);
  std::cout << "All tests passed.\n";
  return 0;
}
