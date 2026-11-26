@echo off
setlocal
set "gameDir=%~dp0..\Binaries\x64\Release\StressTest"
if not exist "%gameDir%\Game.exe" set "gameDir=%~dp0..\Binaries\x64\Release\Game"
if not exist "%gameDir%\Game.exe" (
    echo Release Game.exe not found. Build Game/Game.vcxproj with Release x64 first.
    pause
    exit /b 1
)
start "" /D "%gameDir%" "%gameDir%\Game.exe" -scene Byeongheon/StressTest
