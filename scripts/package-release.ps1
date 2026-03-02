param(
  [string]$Version = "dev",
  [string]$ReleaseDir = "dist",
  [string]$ExePath = "build/campfire.exe"
)

$ErrorActionPreference = "Stop"

$assetNameVersioned = "campfire-win-x64-$Version"
$assetNameStable = "campfire-win-x64"
$stageDir = Join-Path $ReleaseDir $assetNameVersioned
$zipPathVersioned = Join-Path $ReleaseDir "$assetNameVersioned.zip"
$zipPathStable = Join-Path $ReleaseDir "$assetNameStable.zip"

if (-not (Test-Path $ExePath)) {
  throw "Executable not found at $ExePath. Run scripts/build-release.ps1 first."
}

if (Test-Path $stageDir) {
  Remove-Item -Recurse -Force $stageDir
}
New-Item -ItemType Directory -Force $stageDir | Out-Null
New-Item -ItemType Directory -Force $ReleaseDir | Out-Null

Copy-Item $ExePath (Join-Path $stageDir "campfire.exe")
Copy-Item "scripts/install-campfire.ps1" (Join-Path $stageDir "install-campfire.ps1")
Copy-Item "scripts/install-campfire.bat" (Join-Path $stageDir "install-campfire.bat")
if (Test-Path "APP_PATHS.md") {
  Copy-Item "APP_PATHS.md" (Join-Path $stageDir "APP_PATHS.md")
}

if (Test-Path $zipPathVersioned) {
  Remove-Item -Force $zipPathVersioned
}
if (Test-Path $zipPathStable) {
  Remove-Item -Force $zipPathStable
}
Compress-Archive -Path (Join-Path $stageDir "*") -DestinationPath $zipPathVersioned
Copy-Item $zipPathVersioned $zipPathStable

Write-Host "Packaged release: $zipPathVersioned"
Write-Host "Packaged release: $zipPathStable"
