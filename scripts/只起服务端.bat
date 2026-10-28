@echo off
chcp 936 >nul
title 原神 7.1 私服 - 只起服务端（客户端交给帧率解锁器启动）
set "GCSRV=%~dp0..\gcsrv"

echo [1/3] 起服务端（Mongo + grasscutter）...
powershell -NoProfile -ExecutionPolicy Bypass -File "%GCSRV%\start-server.ps1"

echo.
echo [2/3] 起本地 TLS 前置 ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0起TLS前置.ps1"

echo.
echo [3/3] 清掉客户端本地登录痕迹 ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0重置登录状态.ps1"

echo.
echo 服务端和 TLS 前置已就绪。现在用 Genshin FPS Unlocker 启动游戏：
echo   D:\Programs\gs71-runtime\fpsunlock\app\unlockfps_nc.exe
echo 在它里面把 FPS 设成 144，点 Start Game。
echo.
ping -n 8 127.0.0.1 >nul
exit /b 0