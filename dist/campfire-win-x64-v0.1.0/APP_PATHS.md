# Campfire Important Paths

This file documents the key directories and files used by Campfire.

## Runtime Data (most important)

- Game library file (primary): `%LOCALAPPDATA%\Campfire\games.tsv`
- Game library file (fallback): `<current working directory>\data\games.tsv`

Notes:
- Campfire uses `%LOCALAPPDATA%\Campfire\games.tsv` when `LOCALAPPDATA` is available (normal Windows case).
- The fallback path is only used if `LOCALAPPDATA` is not set.

## Executable / Install Paths

- Built executable in this repo: `build\campfire.exe`
- Global installed executable (default installer target): `%USERPROFILE%\bin\campfire.exe`
- Installer script: `scripts\install-campfire.ps1`

## Source Paths

- Entrypoint: `src\main.cpp`
- App runner / top-level error handling: `src\app.cpp`
- Command dispatcher: `src\command_dispatcher.cpp`
- Command handlers: `src\command_handlers.cpp`
- Storage/scanning logic: `src\library.cpp`
- Console formatting/UI helpers: `src\console_ui.cpp`
- CLI arg parsing helpers: `src\cli_args.cpp`

## Headers

- `include\app.hpp`
- `include\command_dispatcher.hpp`
- `include\command_handlers.hpp`
- `include\library.hpp`
- `include\console_ui.hpp`
- `include\cli_args.hpp`

## Build Configuration

- CMake project file: `CMakeLists.txt`
