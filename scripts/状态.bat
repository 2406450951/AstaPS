@echo off
chcp 936 >nul
title ×´Ì¬
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\gcsrv\start-server.ps1" -Status
echo.
echo --- ¸²¸Ç²ã API ---
powershell -NoProfile -Command "try{(Invoke-WebRequest http://127.0.0.1:8890/ping -UseBasicParsing -TimeoutSec 3).Content}catch{'8890 Î´ÏìÓ¦'}"
ping -n 10 127.0.0.1 >nul