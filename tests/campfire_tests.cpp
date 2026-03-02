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

  const fs::path exe_dir = temp_root / "bin";
  fs::create_directories(exe_dir, ec);
  assert_true(!ec, "failed to create temp exe directory");
  const fs::path fake_exe = exe_dir / "game.exe";
  std::ofstream fake_exe_file(fake_exe.string(), std::ios::binary);
  fake_exe_file << "MZ";
  fake_exe_file.close();

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
    const auto games = load_games();
    assert_true(games.size() == 1, "library should contain one unique game");
    assert_true(!games[0].id.empty(), "added game should have an id");
    assert_true(games[0].name == "Test Game", "added game should preserve name");
    assert_true(games[0].args == "-windowed", "added game should preserve launch args");

    const bool removed = remove_game(games[0].id);
    assert_true(removed, "remove_game should remove existing id");
    assert_true(load_games().empty(), "library should be empty after remove");
  }

  fs::remove_all(temp_root, ec);
  std::cout << "All tests passed.\n";
  return 0;
}
