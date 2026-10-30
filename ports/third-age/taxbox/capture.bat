@echo off
rem Runs the game with F3 frame captures enabled. Captures go to captures\ beside this file.
set "XBE_CAPTURE_DIR=%~dp0captures"
rem performance lines in the log once a second (for finding stalls)
set XBE_PERF=1
if not exist "%XBE_CAPTURE_DIR%" mkdir "%XBE_CAPTURE_DIR%"
cd /d "%~dp0out\build\release"
start "" TAXbox.exe
