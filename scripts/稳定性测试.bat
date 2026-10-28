@echo off
title GS71 - soak test
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0?????.ps1" -Minutes 5
ping -n 30 127.0.0.1 >nul