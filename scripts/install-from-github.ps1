param(
  [Parameter(Mandatory = $true)]
  [string]$Repo,
  [string]$Version = "latest"
)

$ErrorActionPreference = "Stop"

# Downloads a Campfire release asset from GitHub and installs it to the user's PATH.
if ($Version -eq "latest") {
  $assetName = "campfire-win-x64.zip"
  $url = "https://github.com/$Repo/releases/latest/download/$assetName"
} else {
  $assetName = "campfire-win-x64-$Version.zip"
  $url = "https://github.com/$Repo/releases/download/$Version/$assetName"
}

$tempRoot = Join-Path $env:TEMP "campfire-install"
$zipPath = Join-Path $tempRoot $assetName
$extractDir = Join-Path $tempRoot "extract"

if (Test-Path $tempRoot) {
  Remove-Item -Recurse -Force $tempRoot
}
New-Item -ItemType Directory -Force $tempRoot | Out-Null

Write-Host "Downloading: $url"
Invoke-WebRequest -Uri $url -OutFile $zipPath

Expand-Archive -Path $zipPath -DestinationPath $extractDir -Force

$sourceExe = Join-Path $extractDir "campfire.exe"
if (-not (Test-Path $sourceExe)) {
  throw "Release asset is missing campfire.exe"
}

$binDir = Join-Path $HOME "bin"
New-Item -ItemType Directory -Force $binDir | Out-Null
$targetExe = Join-Path $binDir "campfire.exe"
Copy-Item -Force $sourceExe $targetExe

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$entries = @()
if ($userPath) {
  $entries = $userPath -split ';' | Where-Object { $_ -ne "" }
}
if ($entries -notcontains $binDir) {
  $newPath = if ($userPath -and $userPath.Trim().Length -gt 0) {
    "$userPath;$binDir"
  } else {
    $binDir
  }
  [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
  Write-Host "Added to user PATH: $binDir"
}

Write-Host "Installed: $targetExe"
Write-Host "Open a new terminal and run: campfire --help"
