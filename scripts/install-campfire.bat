@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "PS1=%SCRIPT_DIR%install-campfire.ps1"

if not exist "%PS1%" (
  echo install-campfire.ps1 was not found next to this file.
  echo Make sure you extracted the full release zip.
  pause
  exit /b 1
)

echo Installing Campfire...
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%"
set "CODE=%ERRORLEVEL%"

if not "%CODE%"=="0" (
  echo.
  echo Installation failed with exit code %CODE%.
  pause
  exit /b %CODE%
)

echo.
echo Campfire installed successfully.
echo Open a NEW terminal and run: campfire --help
pause
