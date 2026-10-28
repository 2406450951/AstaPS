@echo off
title GS71 - background only
set "ROOT=%~dp0.."
set "SERVER=%ROOT%\server"

echo [1/3] server (Mongo + grasscutter) ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%SERVER%\start-server.ps1"

echo.
echo [2/3] local TLS front-end ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0?TLS??.ps1"

echo.
echo [3/3] clear cached login token ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0??????.ps1"

echo.
echo Background is ready. Start the game yourself, or via the FPS unlocker.
ping -n 8 127.0.0.1 >nul
exit /b 0