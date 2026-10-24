@echo off
rem Export simple KEY=VALUE entries to the calling batch file's environment.
rem Blank lines and lines starting with # are ignored; surrounding value quotes are removed.
if not exist "%~dp0..\.env" exit /b 0
for /f "usebackq eol=# tokens=1,* delims==" %%A in ("%~dp0..\.env") do set "%%A=%%~B"
exit /b 0
