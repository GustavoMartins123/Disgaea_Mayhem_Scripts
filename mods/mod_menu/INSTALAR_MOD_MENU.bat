@echo off
setlocal
cd /d "%~dp0"
python INSTALAR_MOD_MENU.py
if errorlevel 1 (
    echo.
    echo Pressione qualquer tecla para sair...
    pause >nul
)
