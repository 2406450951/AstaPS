@echo off
chcp 936 >nul
title 原神 7.1 私服 - 只起服务端（客户端由别的工具启动）
set "ROOT=%~dp0.."
set "SERVER=%ROOT%\server"

echo [1/3] 起服务端（Mongo + grasscutter）...
powershell -NoProfile -ExecutionPolicy Bypass -File "%SERVER%\start-server.ps1"

echo.
echo [2/3] 起本地 TLS 前置 ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0起TLS前置.ps1"

echo.
echo [3/3] 清掉客户端本地登录痕迹 ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0重置登录状态.ps1"

echo.
echo 后台已就绪。现在用帧率解锁器启动游戏（设 GS71_UNLOCKER 指向它的 exe）。
ping -n 8 127.0.0.1 >nul
exit /b 0