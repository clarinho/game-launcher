# Gemini Integration Brief for Campfire Backend

This document is for building an Electron UI against the existing Campfire backend (`campfire.exe`) with minimal integration friction.

## 1) Product Summary

Campfire is a local-first game launcher backend written in C++.
It stores a game library, supports manual game management, scans common install folders, launches games, and tracks basic play stats.

The Electron app should treat Campfire as a subprocess-driven backend (CLI contract).

## 2) Runtime + Storage

- Primary data file: `%LOCALAPPDATA%\\Campfire\\games.tsv`
- Fallback data file (if `LOCALAPPDATA` missing): `<cwd>\\data\\games.tsv`
- Backend binary in this repo: `build\\campfire.exe` (or `build\\campfire_dev.exe` for local dev)

## 3) Current Commands (CLI Contract)

- `campfire add --name "<name>" --exe "<path>" [--args "<args>"]`
- `campfire list`
- `campfire info (--id <id> | --name "<name>")`
- `campfire edit --id <id> [--name "<name>"] [--exe "<path>"] [--args "<args>"]`
- `campfire remove --id <id>`
- `campfire launch (--id <id> | --name "<name>") [--force-fallback]`
- `campfire scan`
- `campfire doctor`
- `campfire --help`

Exit code behavior:
- `0`: success
- non-`0`: failure

Output behavior:
- human-readable stdout for normal results
- human-readable stderr for errors (`[error] ...`)

Note: There is no JSON output mode yet. UI should parse text output conservatively or maintain local view state and use command success/failure + `info/list` refresh after mutations.

## 4) Library Data Schema (TSV)

Each row in `games.tsv` is tab-separated with columns:

1. `id` (string, usually numeric)
2. `name` (string)
3. `exe_path` (primary executable path)
4. `args` (string, may be empty)
5. `play_count` (non-negative int)
6. `total_play_seconds` (non-negative int)
7. `exe_candidates` (optional; `|`-delimited ranked exe list, max 3)

Compatibility:
- Older rows with only first 4 columns still load; missing stats default to `0`.
- If `exe_candidates` is missing, backend treats `exe_path` as the sole candidate.

## 5) Launch + Fallback Semantics

Normal launch (`campfire launch ...`):
- backend builds up to 3 ranked candidates
- attempts #1, then falls back to #2/#3 only if current candidate returns launch error
- if a fallback candidate succeeds, backend promotes it to rank #1 and demotes old primary toward rank #3
- stats are persisted on success

Force fallback (`--force-fallback`):
- same as normal launch, plus this rule for non-Steam launches:
- if a candidate launches but session lasts under 10 seconds, treat as suspicious and try next candidate
- intended to bypass short-lived wrapper/launcher exes

Steam behavior:
- Steam app ID can be auto-detected for executables under Steam directories
- when launched via Steam, session duration is not tracked

## 6) Scan Behavior

`scan` checks these roots:
- `C:\\games`
- `D:\\games`
- `C:\\Program Files (x86)\\Steam\\steamapps\\common`

It chooses one likely executable per game folder and can import found entries interactively.
Current scoring strongly penalizes likely installer/launcher names (e.g., `launcher`, `setup`, `uninstall`, `updater`, etc.).

## 7) Electron Integration Recommendations

## Process Invocation

Use `child_process.spawn` with argument arrays, not shell-concatenated command strings.

Suggested wrapper shape:
- `runCampfire(args: string[]): Promise<{code: number, stdout: string, stderr: string}>`
- resolve on process close
- treat `code !== 0` as error path

Important:
- keep `shell: false`
- preserve exact Windows paths by passing each argument as a separate array item

## UI Data Flow

Recommended approach (without JSON mode):
1. `list` on app load
2. `add/edit/remove` mutate
3. refresh with `list`
4. details screen pulls `info --id <id>`
5. launch action calls `launch --id <id>` (optionally with `--force-fallback` toggle)

## Suggested Frontend Features/Controls

- Library table/cards from `list`
- Add game form (`name`, `exe path`, optional args)
- Edit modal with same fields
- Launch button + optional `Force Fallback` toggle
- Details panel showing:
  - path
  - launch candidates
  - play count
  - time played
- Doctor diagnostics page
- Scan page (manual action; display found candidates and import status)

## 8) Known Constraints for Gemini UI

- Backend is CLI-first; output is text, not structured JSON.
- Some commands are interactive (`scan` asks yes/no in console). For GUI usage, prefer backend enhancement or avoid interactive flow by replacing with explicit add calls.
- Launch success is OS-process based; a wrapper that starts successfully may still open an undesired launcher unless `--force-fallback` detects short sessions.

## 9) Future-Proofing Suggestions (Optional Backend Enhancements)

These are not required to build the first UI, but will simplify integration:
- add `--json` output for `list/info/doctor/scan`
- add non-interactive `scan --import` and `scan --dry-run`
- add explicit command to set/reorder candidate executables
- add machine-readable error codes

## 10) File/Code Map for Gemini

- CLI entry: `src/main.cpp`
- top-level app runner: `src/app.cpp`
- command routing: `src/command_dispatcher.cpp`
- command implementations: `src/command_handlers.cpp`
- storage + scan logic: `src/library.cpp`
- console formatting: `src/console_ui.cpp`
- data model: `include/library.hpp`

