@echo off
chcp 936 >nul
title Í£Ö¹·þÎñ¶Ë
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\gcsrv\start-server.ps1" -Stop
echo ÒÑÍ£Ö¹¡£
ping -n 3 127.0.0.1 >nul