@echo off
chcp 936 >nul
title 原神 7.1 私服 - 启动
set "GCSRV=%~dp0..\gcsrv"
set "GLOBAL=D:\Program Files\HoYoPlay\games\Genshin Impact game"
set "CN=D:\Program Files\miHoYo Launcher_cn\miHoYo Launcher\games\Genshin Impact Game"

rem 自动选择客户端：优先国际服（已验证稳定），没有则用国服
set "GAME="
set "EXE="
if exist "%GLOBAL%\GenshinImpact.exe" (
  set "GAME=%GLOBAL%"
  set "EXE=GenshinImpact.exe"
) else if exist "%CN%\YuanShen.exe" (
  set "GAME=%CN%"
  set "EXE=YuanShen.exe"
)

if "%GAME%"=="" (
  echo   既没找到国际服也没找到国服客户端
  pause
  exit /b 1
)

echo 客户端: %GAME%  ^(%EXE%^)
echo.

echo [1/5] 预检 ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0预检.ps1"

echo.
echo [2/5] 起服务端（Mongo + grasscutter）...
powershell -NoProfile -ExecutionPolicy Bypass -File "%GCSRV%\start-server.ps1"

echo.
echo [3/5] 起本地 TLS 前置 ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0起TLS前置.ps1"

echo.
echo [4/5] 清掉客户端本地登录痕迹 ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0重置登录状态.ps1"

echo.
echo [5/5] 起客户端 ...
if not exist "%GAME%\%EXE%" (
  echo   找不到 %GAME%\%EXE%
  pause
  exit /b 1
)
start "" /D "%GAME%" "%GAME%\%EXE%"

echo.
echo 已启动。登录页里随便输账号密码（记得勾协议）。
echo 进游戏后按 INSERT 呼出作弊菜单。
ping -n 6 127.0.0.1 >nul
exit /b 0