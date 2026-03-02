param(
  [string]$OutputExe = "build/campfire.exe",
  [string]$Compiler = ""
)

$ErrorActionPreference = "Stop"

# Resolves g++ path for local dev and GitHub Actions runners.
function Resolve-CompilerPath {
  param([string]$RequestedCompiler)

  if ($RequestedCompiler -and $RequestedCompiler.Trim().Length -gt 0) {
    if (Test-Path $RequestedCompiler) {
      return $RequestedCompiler
    }
    $requestedCmd = Get-Command $RequestedCompiler -ErrorAction SilentlyContinue
    if ($requestedCmd) {
      return $requestedCmd.Source
    }
  }

  $cmd = Get-Command "g++.exe" -ErrorAction SilentlyContinue
  if ($cmd) {
    return $cmd.Source
  }

  $candidates = @(
    "C:\msys64\ucrt64\bin\g++.exe",
    "D:\a\_temp\msys64\ucrt64\bin\g++.exe",
    (Join-Path $env:RUNNER_TEMP "msys64\ucrt64\bin\g++.exe")
  )

  foreach ($candidate in $candidates) {
    if ($candidate -and (Test-Path $candidate)) {
      return $candidate
    }
  }

  throw "g++.exe not found. Install MSYS2 UCRT64 gcc or pass -Compiler <path>."
}

$resolvedCompiler = Resolve-CompilerPath -RequestedCompiler $Compiler
Write-Host "Using compiler: $resolvedCompiler"

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

& $resolvedCompiler @compilerArgs

if ($LASTEXITCODE -ne 0) {
  throw "Release build failed"
}

Write-Host "Built release executable: $OutputExe"
