@echo off
title GS71 - start everything
set "ROOT=%~dp0.."
set "SERVER=%ROOT%\server"

rem Client dir: set GS71_GAME, or we look in the two usual install locations.
set "GAME="
if defined GS71_GAME set "GAME=%GS71_GAME%"
if not defined GAME if exist "%ProgramFiles%\HoYoPlay\games\Genshin Impact game\GenshinImpact.exe" set "GAME=%ProgramFiles%\HoYoPlay\games\Genshin Impact game"
if not defined GAME if exist "%ProgramFiles%\miHoYo Launcher_cn\miHoYo Launcher\games\Genshin Impact Game\YuanShen.exe" set "GAME=%ProgramFiles%\miHoYo Launcher_cn\miHoYo Launcher\games\Genshin Impact Game"

if not defined GAME (
  echo   Client folder not found. Set GS71_GAME to your install, for example:
  echo     set GS71_GAME=D:\Games\Genshin Impact game
  pause
  exit /b 1
)
if exist "%GAME%\GenshinImpact.exe" (set "EXE=GenshinImpact.exe") else (set "EXE=YuanShen.exe")

echo Client: %GAME%  (%EXE%)
echo.

echo [1/5] preflight ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0??.ps1"

echo.
echo [2/5] server (Mongo + grasscutter) ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%SERVER%\start-server.ps1"

echo.
echo [3/5] local TLS front-end ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0?TLS??.ps1"

echo.
echo [4/5] clear cached login token ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0??????.ps1"

echo.
echo [5/5] launch client ...
start "" /D "%GAME%" "%GAME%\%EXE%"

echo.
echo Done. Log in with any account name, then press INSERT in game.
ping -n 6 127.0.0.1 >nul
exit /b 0