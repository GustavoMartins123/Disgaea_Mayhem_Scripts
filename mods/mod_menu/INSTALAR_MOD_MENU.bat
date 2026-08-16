@echo off
setlocal
cd /d "%~dp0"
"INSTALAR_MOD_MENU.exe"
if errorlevel 1 (
    echo.
    echo Pressione qualquer tecla para sair...
    pause >nul
)
