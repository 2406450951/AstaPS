@echo off
title GS71 - menu side only
rem Starts only the menu side: TLS front-end + token reset. Server must already be running.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0?TLS??.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0??????.ps1"
echo.
echo Done.
pause
exit /b 0