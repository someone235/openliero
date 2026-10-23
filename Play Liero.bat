@echo off
setlocal
set "OPENLIERO_SIGNALING_SERVER=127.0.0.1"
set "OPENLIERO_SIGNALING_PORT=19533"
rem Resolve paths relative to this file, including when launched from a shortcut.
set "game_dir=%~dp0install\windows-x64"
if exist "%game_dir%\openliero.exe" goto launch
set "game_dir=%~dp0install\windows-arm64"
if exist "%game_dir%\openliero.exe" goto launch
set "game_dir=%~dp0install\windows-x64-ci"
if exist "%game_dir%\openliero.exe" goto launch
set "game_dir=%~dp0LIERO"
if exist "%game_dir%\openliero.exe" goto launch
if exist "%game_dir%\openliero.cmd" goto bundled_launcher

echo OpenLiero was not found. Install a Windows build first:
echo   cmake --workflow --preset windows-x64
echo   cmake --install build/windows-x64 --config Release
echo Or extract the Windows portable ZIP into the LIERO folder next to this file.
pause
exit /b 1

:bundled_launcher
call "%game_dir%\openliero.cmd" %*
set "game_exit=%errorlevel%"
goto finished

:launch
pushd "%game_dir%" || exit /b 1
start "" /wait "%game_dir%\openliero.exe" %*
set "game_exit=%errorlevel%"
popd
:finished
if "%game_exit%"=="0" exit /b 0
echo OpenLiero exited with error %game_exit%.
pause
exit /b %game_exit%
