@echo off
chcp 936 >nul
title 原神 7.1 私服 - 只开作弊面板
set "ROOT=%~dp0.."

rem 只起菜单那一侧：TLS 前置 + 清登录痕迹，不碰服务端。
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0起TLS前置.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0重置登录状态.ps1"
echo.
echo 完成。服务端需要你自己已经在跑。
pause
exit /b 0