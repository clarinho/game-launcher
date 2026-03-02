#include "console_ui.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
// Enables ANSI color support on Windows terminals. Non-Windows terminals already support it.
bool enable_ansi() {
#ifdef _WIN32
  const auto out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out == INVALID_HANDLE_VALUE) {
    return false;
  }

  DWORD mode = 0;
  if (!GetConsoleMode(out, &mode)) {
    return false;
  }

  if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0) {
    if (!SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
      return false;
    }
  }
  return true;
#else
  return true;
#endif
}

const bool kUseColor = enable_ansi();

constexpr std::string_view kReset = "\x1b[0m";
constexpr std::string_view kBold = "\x1b[1m";
constexpr std::string_view kCyan = "\x1b[36m";
constexpr std::string_view kGreen = "\x1b[32m";
constexpr std::string_view kYellow = "\x1b[33m";
constexpr std::string_view kRed = "\x1b[31m";
constexpr std::string_view kDim = "\x1b[2m";

std::string color(std::string_view code, const std::string& text) {
  if (!kUseColor) {
    return text;
  }
  return std::string(code) + text + std::string(kReset);
}

std::string truncate_with_ellipsis(const std::string& text, size_t width) {
  if (text.size() <= width) {
    return text;
  }
  if (width <= 3) {
    return text.substr(0, width);
  }
  return text.substr(0, width - 3) + "...";
}

void print_banner() {
  const std::string top = "+-------------------------------------+";
  const std::string mid = "| Campfire - personal game launcher   |";
  std::cout << color(kCyan, top) << '\n'
            << color(kCyan, mid) << '\n'
            << color(kCyan, top) << "\n\n";
}
}  // namespace

namespace ui {
std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool prompt_yes_no(const std::string& prompt) {
  std::cout << color(kBold, prompt) << " (y/N): ";
  std::string reply;
  if (!std::getline(std::cin, reply)) {
    return false;
  }

  const std::string normalized = to_lower(reply);
  return normalized == "y" || normalized == "yes";
}

void print_help() {
  print_banner();
  std::cout
      << "Usage:\n"
      << "  campfire <command> [options]\n\n"
      << "Commands:\n"
      << "  add --name \"<name>\" --exe \"<path>\" [--args \"<args>\"]\n"
      << "  remove --id \"<id>\"\n"
      << "  scan\n"
      << "  list\n"
      << "  --help\n";
}

void print_ok(const std::string& msg) {
  std::cout << color(kGreen, "[ok] ") << msg << '\n';
}

void print_info(const std::string& msg) {
  std::cout << color(kYellow, "[info] ") << msg << '\n';
}

void print_error(const std::string& msg) {
  std::cerr << color(kRed, "[error] ") << msg << '\n';
}

void print_add_result(const Game& game) {
  print_ok("Added game");
  std::cout << "id: " << game.id << '\n';
  std::cout << "name: " << game.name << '\n';
  std::cout << "exe: " << game.exe_path << '\n';
  if (!game.args.empty()) {
    std::cout << "args: " << game.args << '\n';
  }
}

void print_list_table(const std::vector<Game>& games) {
  size_t id_width = 2;
  size_t name_width = 4;
  for (const Game& game : games) {
    id_width = std::max(id_width, game.id.size());
    name_width = std::max(name_width, game.name.size());
  }
  name_width = std::min<size_t>(name_width, 40);

  const std::string divider = std::string(id_width + 2, '-') + "+" + std::string(name_width + 2, '-');
  std::cout << color(kDim, divider) << '\n';
  std::cout << ' ' << std::left << std::setw(static_cast<int>(id_width)) << "ID" << " | "
            << std::left << std::setw(static_cast<int>(name_width)) << "Name" << '\n';
  std::cout << color(kDim, divider) << '\n';

  for (const Game& game : games) {
    std::cout << ' ' << std::left << std::setw(static_cast<int>(id_width)) << game.id << " | "
              << std::left << std::setw(static_cast<int>(name_width))
              << truncate_with_ellipsis(game.name, name_width) << '\n';
  }
}

void print_scan_table(std::vector<ScannedGame> found) {
  std::sort(found.begin(), found.end(), [](const ScannedGame& a, const ScannedGame& b) {
    return a.name < b.name;
  });

  size_t name_width = 4;
  for (const ScannedGame& game : found) {
    name_width = std::max(name_width, game.name.size());
  }
  name_width = std::min<size_t>(name_width, 36);

  const std::string divider = std::string(name_width + 2, '-') + "+" + std::string(75, '-');
  std::cout << color(kDim, divider) << '\n';
  std::cout << ' ' << std::left << std::setw(static_cast<int>(name_width)) << "Name"
            << " | Executable\n";
  std::cout << color(kDim, divider) << '\n';

  for (const ScannedGame& game : found) {
    std::cout << ' ' << std::left << std::setw(static_cast<int>(name_width))
              << truncate_with_ellipsis(game.name, name_width)
              << " | " << game.exe_path << '\n';
  }
}
}  // namespace ui
