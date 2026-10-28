@echo off
chcp 936 >nul
title 原神 7.1 私服 - 启动（带帧率解锁）
set "UNLOCKER=unlockfps_nc.exe"
if defined GS71_UNLOCKER set "UNLOCKER=%GS71_UNLOCKER%"

start "" "%~dp0只起服务端.bat"
if exist "%UNLOCKER%" (
  start "" "%UNLOCKER%"
) else (
  where %UNLOCKER% >nul 2>&1 && start "" %UNLOCKER% || echo   没找到帧率解锁器：设 GS71_UNLOCKER 指向 unlockfps_nc.exe
)
exit /b 0