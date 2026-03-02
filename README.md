# Campfire

Campfire is a local-first personal game launcher for Windows.
It lets you maintain a game library, launch titles from one place, and track simple play stats.

## Installation

### One-Click Install (Recommended for friends)

1. Open the latest GitHub Release.
2. Download `Campfire-Setup.exe`.
3. Run the installer.
4. Launch **Campfire** from Start Menu.

### One-Click Install (CLI only)

1. Open the latest GitHub Release.
2. Download `campfire-win-x64.zip`.
3. Extract the zip.
4. Double-click `install-campfire.bat`.
5. Open a new terminal and run:

```bash
campfire --help
```

### Prerequisites

- Windows 10/11
- C++ compiler toolchain for building the backend
- Node.js 18+ and npm (for the Electron UI)

### Clone

```bash
git clone <your-repo-url>
cd game-launcher
```

### Build backend (Campfire CLI)

Using CMake:

```bash
cmake -S . -B build
cmake --build build --config Release
```

Or quick local build (MSYS2 `g++`):

```bash
g++ -std=c++17 -Iinclude src/*.cpp -o build/campfire_dev.exe
```

### Run the Electron UI

```bash
cd ui
npm install
npm run dev
```

Notes:
- The UI auto-reloads/restarts during development (`npm run dev`).
- The UI prefers `build/campfire_dev.exe`, then falls back to `build/campfire.exe`.

### Build the Windows UI installer locally

```bash
cd ui
npm install
npm run dist:win
```

Installer output:
- `dist/ui/*.exe`

## Features

- Add games by name + executable path
- Edit existing game metadata (`name`, `exe`, `args`)
- Remove games by ID
- List library entries in a table
- Show detailed info for one game
- Launch by ID or exact name
- Track play count + total play time
- Detect and launch Steam installs via app ID when applicable
- Confidence-ranked executable fallback (up to 3 candidates)
- Optional `--force-fallback` launch mode for short-session wrapper detection
- Scan common install directories for likely game executables
- Run local diagnostics with `doctor`

## Commands

```bash
campfire add --name "<name>" --exe "<path>" [--args "<args>"]
campfire list
campfire info (--id <id> | --name "<name>")
campfire edit --id <id> [--name "<name>"] [--exe "<path>"] [--args "<args>"]
campfire remove --id <id>
campfire launch (--id <id> | --name "<name>") [--force-fallback]
campfire scan
campfire doctor
campfire --help
```

## Launch Fallback Model

Each game stores a ranked executable list (max 3):

- rank #1: primary executable (`exe_path`)
- rank #2/#3: fallback candidates (`exe_candidates`)

On launch:

1. Campfire tries candidate #1.
2. If it fails to start, Campfire tries #2, then #3.
3. If a fallback succeeds, it is promoted to rank #1.
4. The previous rank #1 is moved down in the confidence order.

### `--force-fallback`

`--force-fallback` adds an extra rule for non-Steam launches:

- if a launched candidate exits in under 10 seconds, Campfire treats it as suspicious and tries the next candidate.

This helps bypass launcher-wrapper executables that open another app and exit immediately.

## Storage

Campfire stores data in a TSV file:

- primary path: `%LOCALAPPDATA%\Campfire\games.tsv`
- fallback path: `./data/games.tsv` (if `LOCALAPPDATA` is unset)

### TSV Columns

1. `id`
2. `name`
3. `exe_path`
4. `args`
5. `play_count`
6. `total_play_seconds`
7. `exe_candidates` (optional, pipe-delimited)

Legacy rows with fewer columns are still supported.

## Scan Roots

`scan` currently inspects:

- `C:\games`
- `D:\games`
- `C:\Program Files (x86)\Steam\steamapps\common`

Executable scoring prefers likely game binaries and penalizes installer/launcher-like names.

## Build

### CMake

```bash
cmake -S . -B build
cmake --build build
```

### Quick compile (MSYS2 g++)

```bash
g++ -std=c++17 -Iinclude src/*.cpp -o build/campfire_dev.exe
```

## Test

If tests are enabled in CMake:

```bash
ctest --test-dir build
```

## Project Layout

- `src/main.cpp`: process entrypoint
- `src/app.cpp`: top-level exception boundary
- `src/command_dispatcher.cpp`: command routing
- `src/command_handlers.cpp`: command logic
- `src/library.cpp`: persistence + scanning
- `src/console_ui.cpp`: terminal output helpers
- `include/library.hpp`: core game model
- `tests/campfire_tests.cpp`: integration-style command tests

## Notes

- The current interface is CLI-first and text-output based.
- A future JSON output mode would make GUI integrations more robust.
