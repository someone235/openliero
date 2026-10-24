@echo off
setlocal DisableDelayedExpansion
call "%~dp0scripts\load-env.bat"
if not defined OPENLIERO_SIGNALING_PORT set "OPENLIERO_SIGNALING_PORT=19533"
pushd "%~dp0server" || exit /b 1

rem Rebuild when Go is available; otherwise use the existing executable.
where go >nul 2>&1
if errorlevel 1 goto check_binary
echo Building OpenLiero server...
go build -o openliero-relay.exe .
if errorlevel 1 goto build_failed

:check_binary
if not exist "%~dp0server\openliero-relay.exe" goto missing_binary
echo Starting OpenLiero signaling server on UDP port %OPENLIERO_SIGNALING_PORT%.
echo Keep this window open while playing. Press Ctrl+C to stop the server.
"%~dp0server\openliero-relay.exe" --port "%OPENLIERO_SIGNALING_PORT%" %*
set "server_exit=%errorlevel%"
popd
if "%server_exit%"=="0" exit /b 0
echo OpenLiero server exited with error %server_exit%.
pause
exit /b %server_exit%

:missing_binary
echo OpenLiero server is missing. Install Go 1.26.3 or newer, then run this file again.
goto failed

:build_failed
echo OpenLiero server could not be built. See the errors above.

:failed
popd
pause
exit /b 1
