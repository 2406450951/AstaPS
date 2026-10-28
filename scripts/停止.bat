@echo off
title GS71 - stop
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\server\start-server.ps1" -Stop
echo Stopped.
ping -n 3 127.0.0.1 >nul