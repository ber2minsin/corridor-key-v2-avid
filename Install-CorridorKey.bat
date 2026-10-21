@echo off
:: Developer fallback installer (PowerShell). Prefer Setup-CorridorKeyAVX.exe from packaging\dist.
:: Double-click Setup-CorridorKeyAVX.exe for the normal editor install path.

setlocal
set "EXE=%~dp0packaging\dist\Setup-CorridorKeyAVX.exe"
if exist "%EXE%" (
  echo Found Setup EXE — launching that instead of the .bat path.
  echo.
  "%EXE%"
  exit /b %ERRORLEVEL%
)

echo Setup-CorridorKeyAVX.exe not built yet.
echo Building it requires: scripts\build_installer.ps1
echo.
echo Falling back to elevated PowerShell installer for developers...
echo.

net session >nul 2>&1
if %errorLevel% neq 0 (
  echo Requesting administrator permission...
  powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0packaging\Install-CorridorKeyAVX.ps1"
if errorlevel 1 (
  echo.
  echo Install failed. See the message above.
  pause
  exit /b 1
)
echo.
pause
