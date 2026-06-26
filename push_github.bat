@echo off
setlocal
cd /d "%~dp0"

set GH=C:\Program Files\GitHub CLI\gh.exe
if not exist "%GH%" (
    echo GitHub CLI non trovato. Installa con: winget install GitHub.cli
    pause
    exit /b 1
)

echo Verifica accesso GitHub...
"%GH%" auth status >nul 2>&1
if errorlevel 1 (
    echo.
    echo --- Accedi a GitHub nel browser ---
    "%GH%" auth login --hostname github.com --git-protocol https --web
    if errorlevel 1 (
        echo Login fallito.
        pause
        exit /b 1
    )
)

echo.
echo Creazione repository e push su GitHub...
"%GH%" repo create TrueAudioRand --public --description "TRNG da rumore di microfono (C + PortAudio + GUI Win32)" --source=. --remote=origin --push

if errorlevel 1 (
    echo.
    echo Se il repository esiste gia, prova:
    echo   git remote add origin https://github.com/TUO_USERNAME/TrueAudioRand.git
    echo   git push -u origin main
)

echo.
pause
