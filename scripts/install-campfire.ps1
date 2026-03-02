param(
  [string]$BuildDir = "build",
  [string]$BinDir = "$HOME\\bin"
)

$ErrorActionPreference = "Stop"

# Support two layouts:
# 1) Repo layout: scripts/install-campfire.ps1 + build/campfire.exe
# 2) Shared zip layout: install-campfire.ps1 next to campfire.exe
$zipExe = Join-Path $PSScriptRoot "campfire.exe"
$repoRoot = Split-Path -Parent $PSScriptRoot
$repoExe = Join-Path $repoRoot (Join-Path $BuildDir "campfire.exe")

$sourceExe = $null
if (Test-Path $zipExe) {
  $sourceExe = $zipExe
} elseif (Test-Path $repoExe) {
  $sourceExe = $repoExe
}

if (-not $sourceExe) {
  throw "campfire.exe not found. Expected either '$zipExe' or '$repoExe'."
}

New-Item -ItemType Directory -Force $BinDir | Out-Null
$targetExe = Join-Path $BinDir "campfire.exe"
Copy-Item -Force $sourceExe $targetExe

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$pathEntries = @()
if ($userPath) {
  $pathEntries = $userPath -split ';' | Where-Object { $_ -ne "" }
}

if ($pathEntries -notcontains $BinDir) {
  $newPath = if ($userPath -and $userPath.Trim().Length -gt 0) {
    "$userPath;$BinDir"
  } else {
    $BinDir
  }

  [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
  Write-Host "Added to user PATH: $BinDir"
} else {
  Write-Host "Already on user PATH: $BinDir"
}

Write-Host "Installed: $targetExe"
Write-Host "Open a new terminal and run: campfire --help"
