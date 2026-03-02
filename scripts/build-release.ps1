param(
  [string]$OutputExe = "build/campfire.exe",
  [string]$Compiler = "C:\msys64\ucrt64\bin\g++.exe"
)

$ErrorActionPreference = "Stop"

# Build a release binary with static runtime linkage where possible.
# This minimizes redistributable DLL requirements on target machines.
$compilerArgs = @(
  "-std=c++20",
  "-Iinclude",
  "-static",
  "-static-libgcc",
  "-static-libstdc++",
  "src/app.cpp",
  "src/command_dispatcher.cpp",
  "src/command_handlers.cpp",
  "src/main.cpp",
  "src/library.cpp",
  "src/console_ui.cpp",
  "src/cli_args.cpp",
  "-o",
  $OutputExe
)

& $Compiler @compilerArgs

if ($LASTEXITCODE -ne 0) {
  throw "Release build failed"
}

Write-Host "Built release executable: $OutputExe"
