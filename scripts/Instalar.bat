@echo off
chcp 65001 >nul
echo ============================================
echo   Bronco - Instalador
echo ============================================
echo.

:: Detectar PowerShell 7 (pwsh) ou fallback para PowerShell 5.1
where pwsh >nul 2>&1
if %ERRORLEVEL% == 0 (
    echo Usando PowerShell 7...
    pwsh -ExecutionPolicy Bypass -File "%~dp0install.ps1"
) else (
    echo Usando PowerShell 5.1...
    powershell -ExecutionPolicy Bypass -File "%~dp0install.ps1"
)

echo.
pause
