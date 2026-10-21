@echo off
:: Quit Media Composer first, then double-click this.
net session >nul 2>&1
if %errorLevel% neq 0 (
  echo Requesting administrator permission...
  powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\deploy_plugin.ps1"
if errorlevel 1 (
  echo.
  echo Deploy failed. Quit Media Composer fully and try again.
  pause
  exit /b 1
)
echo.
pause
