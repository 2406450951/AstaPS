@echo off
title GS71 - status
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\server\start-server.ps1" -Status
echo.
echo --- cheat API ---
powershell -NoProfile -Command "try{(Invoke-WebRequest http://127.0.0.1:8890/ping -UseBasicParsing -TimeoutSec 3).Content}catch{'8890 not answering'}"
ping -n 10 127.0.0.1 >nul