@echo off
chcp 65001 >nul
title Установка Poppler для Black's Law Dictionary Search

echo Запуск установщика зависимостей...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1"
pause