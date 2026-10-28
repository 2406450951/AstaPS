@echo off
title GS71 - login chain test
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0?????.ps1"
ping -n 25 127.0.0.1 >nul