@echo off
chcp 936 >nul
title GS71 µÇÂ¼Á´²âÊÔ
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0µÇÂ¼Á´²âÊÔ.ps1"
ping -n 25 127.0.0.1 >nul
