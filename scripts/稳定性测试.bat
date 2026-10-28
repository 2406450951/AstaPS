@echo off
chcp 936 >nul
title GS71 稳定性测试
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0稳定性测试.ps1" -Minutes 5
ping -n 30 127.0.0.1 >nul
