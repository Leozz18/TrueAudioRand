@echo off
setlocal
cd /d "%~dp0"
set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%

echo Compilazione completa...
mingw32-make clean
mingw32-make all
if errorlevel 1 (
    echo Compilazione fallita.
    pause
    exit /b 1
)

start "" /D "%~dp0" "%~dp0TrueAudioRand.exe"
