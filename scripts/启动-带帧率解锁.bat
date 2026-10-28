@echo off
title GS71 - start with FPS unlock
set "UNLOCKER=unlockfps_nc.exe"
if defined GS71_UNLOCKER set "UNLOCKER=%GS71_UNLOCKER%"

start "" "%~dp0?????.bat"
if exist "%UNLOCKER%" (
  start "" "%UNLOCKER%"
) else (
  where %UNLOCKER% >nul 2>&1 && start "" %UNLOCKER% || echo   FPS unlocker not found, set GS71_UNLOCKER to unlockfps_nc.exe
)
exit /b 0