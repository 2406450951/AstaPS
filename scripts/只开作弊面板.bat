@echo off
chcp 936 >nul
title 只开作弊面板（旧版外部窗口）
set "GCSRV=%~dp0..\gcsrv"
set "PYW=D:\Programs\gs71-runtime\python311\pythonw.exe"
if not exist "%PYW%" set "PYW=pythonw"
powershell -NoProfile -WindowStyle Hidden -Command "Start-Process -FilePath '%PYW%' -ArgumentList '%GCSRV%\tools\cheatpanel.py'"
exit /b 0