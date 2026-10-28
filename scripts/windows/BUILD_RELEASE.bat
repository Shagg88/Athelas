@echo off
rem BUILD_RELEASE.bat - one double-click for the next Athelas release (C:\Athelas\BUILD_RELEASE.bat starts this).
rem Brings source\ up to date from backup\athelas.bundle (Claude's latest), then builds The Two Towers, The Return of
rem the King and The Fellowship of the Ring (x64, then ARM64) in place under source\ports, reading the game files from
rem games\xbox-us. Everything lands in build\release-out. No questions; it stops at the first real problem and says
rem which game and which step. Nothing is pushed or published.
setlocal EnableExtensions
for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
set "SRC=%ROOT%\source"
set "TT_GAME=%ROOT%\games\xbox-us\two-towers"
set "RX_GAME=%ROOT%\games\xbox-us\return-of-the-king"
set "FX_GAME=%ROOT%\games\xbox-us\fellowship"
set "OUTDIR=%ROOT%\build\release-out"
set "INFO=%OUTDIR%\BUILD_INFO.txt"
set "ARM_NOTE="

call :find_git || (set "STEP=Git was not found - install Git for Windows" & goto :failed)
if not exist "%SRC%\.git" (set "STEP=%SRC% is not the Athelas repository" & goto :failed)
if not exist "%ROOT%\backup\athelas.bundle" (set "STEP=backup\athelas.bundle is missing, so the source can't be brought up to date - ask Claude to back it up again" & goto :failed)
call "%~dp0UPDATE_SOURCE.bat" quiet || (set "STEP=bringing source\ up to date from backup\athelas.bundle" & goto :failed)
for %%G in ("%TT_GAME%" "%RX_GAME%" "%FX_GAME%") do if not exist "%%~G\default.xbe" (set "STEP=The game files were not found at %%~G" & goto :failed)

rd /s /q "%OUTDIR%" 2>nul
mkdir "%OUTDIR%" || (set "STEP=could not make %OUTDIR%" & goto :failed)
echo Athelas release build, %DATE% %TIME%> "%INFO%"
for /f %%C in ('git -C "%SRC%" rev-parse --short HEAD') do echo source at %%C>> "%INFO%"

rem the game files: each port's assets\default.xbe, and redirect.txt naming the folder with the rest of the game
call :assets "%SRC%\ports\two-towers\game" "%TT_GAME%" || goto :failed
call :assets "%SRC%\ports\rotk-xbox\rotkxbox" "%RX_GAME%" || goto :failed
call :assets "%SRC%\ports\fellowship\fotrxbox" "%FX_GAME%" || goto :failed

echo.
echo === The Two Towers: building (after a code change the first build recompiles the game: 30 to 90 minutes) ===
set CI=1
call "%SRC%\ports\two-towers\game\build.bat" || (set "STEP=The Two Towers: build.bat" & goto :failed)
set CI=1
call "%SRC%\ports\two-towers\game\release.bat" || (set "STEP=The Two Towers: release.bat" & goto :failed)
call :collect "%SRC%\ports\two-towers\game\out\build\release\TwoTowers" TT-Xbox TwoTowers || goto :failed

echo.
echo === The Return of the King: building ===
set CI=1
call "%SRC%\ports\rotk-xbox\rotkxbox\release.bat" || (set "STEP=The Return of the King: release.bat" & goto :failed)
call :collect "%SRC%\ports\rotk-xbox\rotkxbox\release\ROTKXbox" ROTK-Xbox ROTKXbox || goto :failed

echo.
echo === The Fellowship of the Ring: building ===
set CI=1
call "%SRC%\ports\fellowship\fotrxbox\release.bat" || (set "STEP=The Fellowship of the Ring: release.bat" & goto :failed)
call :collect "%SRC%\ports\fellowship\fotrxbox\release\FOTRXbox" FOTR-Xbox FOTRXbox || goto :failed

echo.
echo === ARM64 builds (a failure here is only a warning) ===
mkdir "%OUTDIR%\arm64"
call :arm "%SRC%\ports\two-towers\game" TwoTowers "The Two Towers"
call :arm "%SRC%\ports\rotk-xbox\rotkxbox" ROTKXbox "The Return of the King"
call :arm "%SRC%\ports\fellowship\fotrxbox" FOTRXbox "The Fellowship of the Ring"

echo.
echo ==========================================================================
echo   ALL BUILT. Everything is in: %OUTDIR%
if defined ARM_NOTE echo   Note: %ARM_NOTE%
echo.
type "%INFO%"
echo.
echo   Next: tell Claude it's done. Nothing has been pushed or published.
echo ==========================================================================
pause
exit /b 0

:assets
if not exist "%~1\assets" mkdir "%~1\assets"
copy /y "%~2\default.xbe" "%~1\assets\default.xbe" >nul || (set "STEP=copying %~2\default.xbe" & exit /b 1)
echo %~2> "%~1\assets\redirect.txt"
exit /b 0

:collect
if not exist "%~1\%~3.exe" (set "STEP=%~3.exe was not found in %~1" & exit /b 1)
xcopy /e /i /q /y "%~1" "%OUTDIR%\%~2" >nul || (set "STEP=copying %~1" & exit /b 1)
for /f "delims=" %%H in ('certutil -hashfile "%OUTDIR%\%~2\%~3.exe" SHA256 ^| findstr /v ":"') do echo %~2\%~3.exe  %%H>> "%INFO%"
echo   Copied to release-out\%~2
exit /b 0

:arm
set CI=1
call "%~1\build_arm64.bat"
if errorlevel 1 (set "ARM_NOTE=%ARM_NOTE% %~3 ARM64 build failed; the rest is fine." & exit /b 0)
copy /y "%~1\out\build\release-arm64\%~2-arm64.exe" "%OUTDIR%\arm64\" >nul
for /f "delims=" %%H in ('certutil -hashfile "%OUTDIR%\arm64\%~2-arm64.exe" SHA256 ^| findstr /v ":"') do echo arm64\%~2-arm64.exe  %%H>> "%INFO%"
exit /b 0

:find_git
where git >nul 2>nul && exit /b 0
for /d %%V in ("%ProgramFiles%\Microsoft Visual Studio\*") do for /d %%E in ("%%V\*") do (
  if exist "%%E\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe" set "PATH=%%E\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd;%PATH%"
)
where git >nul 2>nul
exit /b

:failed
echo.
echo ==========================================================================
echo   STOPPED: %STEP%
echo   Scroll up for the details. Nothing has been pushed or published.
echo ==========================================================================
pause
exit /b 1
