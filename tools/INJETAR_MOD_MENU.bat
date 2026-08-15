@echo off
setlocal
title Disgaea Mayhem - Mod Menu
cd /d "%~dp0"
python INJETAR_MOD_MENU.py
set "MOD_MENU_EXIT=%ERRORLEVEL%"
echo.
if not "%MOD_MENU_EXIT%"=="0" echo Falha ao injetar o Mod Menu. Codigo: %MOD_MENU_EXIT%
pause
exit /b %MOD_MENU_EXIT%
