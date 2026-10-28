@echo off
chcp 936 >nul
title 保持唤醒（跑私服时开着这个）
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0保持唤醒.ps1"
