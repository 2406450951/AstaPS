@echo off
chcp 936 >nul
title 原神 7.1 私服 - 启动
set "ROOT=%~dp0.."
set "SERVER=%ROOT%\server"

rem ---- 客户端目录 ----
rem 默认在两个常见位置找；也可以用环境变量 GS71_GAME 指定。
set "GAME="
if defined GS71_GAME set "GAME=%GS71_GAME%"
if not defined GAME if exist "%ProgramFiles%\HoYoPlay\games\Genshin Impact game\GenshinImpact.exe" set "GAME=%ProgramFiles%\HoYoPlay\games\Genshin Impact game"
if not defined GAME if exist "%ProgramFiles%\miHoYo Launcher_cn\miHoYo Launcher\games\Genshin Impact Game\YuanShen.exe" set "GAME=%ProgramFiles%\miHoYo Launcher_cn\miHoYo Launcher\games\Genshin Impact Game"

if not defined GAME (
  echo   没找到客户端目录。设环境变量 GS71_GAME 指向它，例如：
  echo     set GS71_GAME=D:\Games\Genshin Impact game
  pause
  exit /b 1
)
if exist "%GAME%\GenshinImpact.exe" (set "EXE=GenshinImpact.exe") else (set "EXE=YuanShen.exe")

echo 客户端: %GAME%  (%EXE%)
echo.

echo [1/5] 预检 ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0预检.ps1"

echo.
echo [2/5] 起服务端（Mongo + grasscutter）...
powershell -NoProfile -ExecutionPolicy Bypass -File "%SERVER%\start-server.ps1"

echo.
echo [3/5] 起本地 TLS 前置 ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0起TLS前置.ps1"

echo.
echo [4/5] 清掉客户端本地登录痕迹 ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0重置登录状态.ps1"

echo.
echo [5/5] 起客户端 ...
start "" /D "%GAME%" "%GAME%\%EXE%"

echo.
echo 已启动。登录页随便输账号密码（记得勾协议），进游戏后按 INSERT 呼出菜单。
ping -n 6 127.0.0.1 >nul
exit /b 0