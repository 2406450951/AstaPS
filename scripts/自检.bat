@echo off
chcp 936 >nul
title GS71 вт╪Л
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0вт╪Л.ps1"
ping -n 25 127.0.0.1 >nul
