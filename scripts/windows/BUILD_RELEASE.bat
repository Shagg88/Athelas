@echo off
rem BUILD_RELEASE.bat - one double-click for the next Athelas release (C:\Athelas\BUILD_RELEASE.bat starts this).
rem Brings source\ up to date from backup\athelas.bundle (Claude's latest), then builds The Two Towers, The Return of
rem the King, The Fellowship of the Ring and The Third Age (x64, then ARM64) in place under source\ports, reading the game files from
rem games\xbox-us. Everything lands in build\release-out. No questions; it stops at the first real problem and says
rem which game and which step. Nothing is pushed or published.
rem Since 1.7.5 the port programs ship again (Shagg, 30 Oct: the launcher's build from kits failed for players in
rem 1.7.4): each port's folder goes in with its .exe, and arm64\<exe>-arm64.exe for Windows on ARM. No kits.
setlocal EnableExtensions
rem started by a job or a schedule (CI=1 from the caller): no "press a key" at the end
if defined CI set "BR_UNATTENDED=1"
rem UPDATE_SOURCE rewrites this very file, and cmd reads a running .bat from a byte position: so it always runs from a
rem copy in %TEMP% (BR_HOME = this folder), and after the update the new version starts again from a fresh copy
rem (no parenthesised block: a %TEMP% with a ")" in it, as in a user name "Foo (Admin)", would break one)
if defined BR_COPY goto :br_running
set "BR_HOME=%~dp0"
set "BR_RUN=%TEMP%\athelas-build-release-%RANDOM%%RANDOM%.bat"
set "BR_COPY=%TEMP%\athelas-build-release-%RANDOM%%RANDOM%-updated.bat"
copy /y "%~f0" "%BR_RUN%" >nul || goto :br_copyfail
rem one line, read once: this file may be rewritten while the copy runs. The copies go afterwards, the result kept.
call "%BR_RUN%" %* & if errorlevel 1 (del "%BR_RUN%" "%BR_COPY%" 2>nul & exit /b 1) else (del "%BR_RUN%" "%BR_COPY%" 2>nul & exit /b 0)
:br_copyfail
echo Could not copy BUILD_RELEASE.bat to the TEMP folder.
if not defined BR_UNATTENDED pause
exit /b 1
:br_running
for %%I in ("%BR_HOME%..\..\..") do set "ROOT=%%~fI"
set "SRC=%ROOT%\source"
set "TT_GAME=%ROOT%\games\xbox-us\two-towers"
set "RX_GAME=%ROOT%\games\xbox-us\return-of-the-king"
set "FX_GAME=%ROOT%\games\xbox-us\fellowship"
set "TA_GAME=%ROOT%\games\xbox-us\third-age"
set "OUTDIR=%ROOT%\build\release-out"
set "INFO=%OUTDIR%\BUILD_INFO.txt"
set "ARM_NOTE="

call :find_git || (set "STEP=Git was not found - install Git for Windows" & goto :failed)
if not exist "%SRC%\.git" (set "STEP=%SRC% is not the Athelas repository" & goto :failed)
if not exist "%ROOT%\backup\athelas.bundle" (set "STEP=backup\athelas.bundle is missing, so the source can't be brought up to date - ask Claude to back it up again" & goto :failed)
if not defined BR_UPDATED (
  call "%BR_HOME%UPDATE_SOURCE.bat" quiet < nul || (set "STEP=bringing source\ up to date from backup\athelas.bundle" & goto :failed)
  rem start the updated BUILD_RELEASE.bat again (from its own fresh copy), without updating twice
  set "BR_UPDATED=1"
  copy /y "%BR_HOME%BUILD_RELEASE.bat" "%BR_COPY%" >nul || (set "STEP=copying the updated BUILD_RELEASE.bat" & goto :failed)
  call "%BR_COPY%" %*
  exit /b
)
for %%G in ("%TT_GAME%" "%RX_GAME%" "%FX_GAME%" "%TA_GAME%") do if not exist "%%~G\default.xbe" (set "STEP=The game files were not found at %%~G" & goto :failed)

rd /s /q "%OUTDIR%" 2>nul
mkdir "%OUTDIR%" || (set "STEP=could not make %OUTDIR%" & goto :failed)
echo Athelas release build, %DATE% %TIME%> "%INFO%"
for /f %%C in ('git -C "%SRC%" rev-parse --short HEAD') do echo source at %%C>> "%INFO%"

rem the game files: each port's assets\default.xbe, and redirect.txt naming the folder with the rest of the game
call :assets "%SRC%\ports\two-towers\game" "%TT_GAME%" || goto :failed
call :assets "%SRC%\ports\rotk-xbox\rotkxbox" "%RX_GAME%" || goto :failed
call :assets "%SRC%\ports\fellowship\fotrxbox" "%FX_GAME%" || goto :failed
call :assets "%SRC%\ports\third-age\taxbox" "%TA_GAME%" || goto :failed

rem llvm-mingw (both hosts) for the kits and the zips' build tools: tools\llvm-mingw and tools\llvm-mingw-arm64host
set "LLVM_MINGW=%ROOT%\tools\llvm-mingw"
if not exist "%LLVM_MINGW%\bin\clang.exe" (set "STEP=llvm-mingw is missing from %LLVM_MINGW% - ask Claude to fetch it" & goto :failed)
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VSDIR=%%i"
set "PATH=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%LLVM_MINGW%\bin;%PATH%"

echo.
echo === The Two Towers: building (after a code change the first build recompiles the game: 30 to 90 minutes) ===
set CI=1
call "%SRC%\ports\two-towers\game\build.bat" || (set "STEP=The Two Towers: build.bat" & goto :failed)
set CI=1
call "%SRC%\ports\two-towers\game\release.bat" || (set "STEP=The Two Towers: release.bat" & goto :failed)
call :collect "%SRC%\ports\two-towers\game\out\build\release\TwoTowers" TT-Xbox TwoTowers || goto :failed
call :mingw "%SRC%\ports\two-towers\game" TT-Xbox TwoTowers || goto :failed

echo.
echo === The Return of the King: building ===
set CI=1
call "%SRC%\ports\rotk-xbox\rotkxbox\release.bat" || (set "STEP=The Return of the King: release.bat" & goto :failed)
call :collect "%SRC%\ports\rotk-xbox\rotkxbox\release\ROTKXbox" ROTK-Xbox ROTKXbox || goto :failed
call :mingw "%SRC%\ports\rotk-xbox\rotkxbox" ROTK-Xbox ROTKXbox || goto :failed

echo.
echo === The Fellowship of the Ring: building ===
set CI=1
call "%SRC%\ports\fellowship\fotrxbox\release.bat" || (set "STEP=The Fellowship of the Ring: release.bat" & goto :failed)
call :collect "%SRC%\ports\fellowship\fotrxbox\release\FOTRXbox" FOTR-Xbox FOTRXbox || goto :failed
call :mingw "%SRC%\ports\fellowship\fotrxbox" FOTR-Xbox FOTRXbox || goto :failed

echo.
echo === The Third Age: building ===
set CI=1
call "%SRC%\ports\third-age\taxbox\release.bat" || (set "STEP=The Third Age: release.bat" & goto :failed)
call :collect "%SRC%\ports\third-age\taxbox\release\TAXbox" TA-Xbox TAXbox || goto :failed
call :mingw "%SRC%\ports\third-age\taxbox" TA-Xbox TAXbox || goto :failed

rem (the ARM64 programs are in arm64\, for the ARM64 zip)

echo.
echo ==========================================================================
echo   ALL BUILT. Everything is in: %OUTDIR%
if defined ARM_NOTE echo   Note: %ARM_NOTE%
echo.
type "%INFO%"
echo.
echo   Next: tell Claude it's done. Nothing has been pushed or published.
echo ==========================================================================
if not defined BR_UNATTENDED pause
exit /b 0

:assets
if not exist "%~1\assets" mkdir "%~1\assets"
copy /y "%~2\default.xbe" "%~1\assets\default.xbe" >nul || (set "STEP=copying %~2\default.xbe" & exit /b 1)
echo %~2> "%~1\assets\redirect.txt"
exit /b 0

:collect
if not exist "%~1\%~3.exe" (set "STEP=%~3.exe was not found in %~1" & exit /b 1)
xcopy /e /i /q /y "%~1" "%OUTDIR%\%~2" >nul || (set "STEP=copying %~1" & exit /b 1)
rem (its checksum is written by :mingw, whose llvm-mingw program replaces this one)
del /q "%OUTDIR%\%~2\*.pdb" 2>nul
echo   Copied to release-out\%~2 (with %~3.exe)
exit /b 0

:mingw
rem the shipped programs are llvm-mingw builds (x64 and ARM64): the PGO profiles in pgo\ are made with that clang
rem (Visual Studio's clang can't read them), so the x64 one replaces the Visual Studio build release.bat copied
pushd "%~1"
for %%P in (mingw mingw-arm64) do (
  cmake --preset %%P >nul || (popd & set "STEP=%~2: configuring %%P" & exit /b 1)
  cmake --build out\build\%%P --target %~3 || (popd & set "STEP=%~2: building %%P" & exit /b 1)
)
popd
copy /y "%~1\out\build\mingw\%~3.exe" "%OUTDIR%\%~2\%~3.exe" >nul || (set "STEP=%~2: copying the x64 program" & exit /b 1)
set "BR_H="
for /f "delims=" %%H in ('certutil -hashfile "%OUTDIR%\%~2\%~3.exe" SHA256 ^| findstr /v ":"') do set "BR_H=%%H"
if not defined BR_H (set "STEP=%~2: hashing the x64 program" & exit /b 1)
rem (older Windows' certutil prints the hash with spaces between the bytes)
set "BR_H=%BR_H: =%"
echo %BR_H%  %~3.exe> "%OUTDIR%\%~2\%~3.exe.sha256"
echo %~2\%~3.exe  %BR_H%  ^(llvm-mingw, PGO^)>> "%INFO%"
rem the symbols for the program that actually ships (not release.bat's Visual Studio build): symbols\<sha256>
if not exist "%~1\symbols\%BR_H%" mkdir "%~1\symbols\%BR_H%"
copy /y "%~1\out\build\mingw\%~3.exe" "%~1\symbols\%BR_H%\" >nul
if exist "%~1\out\build\mingw\%~3.pdb" copy /y "%~1\out\build\mingw\%~3.pdb" "%~1\symbols\%BR_H%\" >nul
if not exist "%OUTDIR%\arm64" mkdir "%OUTDIR%\arm64"
copy /y "%~1\out\build\mingw-arm64\%~3.exe" "%OUTDIR%\arm64\%~3-arm64.exe" >nul || (set "STEP=%~2: copying the ARM64 program" & exit /b 1)
set "BR_H="
for /f "delims=" %%H in ('certutil -hashfile "%OUTDIR%\arm64\%~3-arm64.exe" SHA256 ^| findstr /v ":"') do set "BR_H=%%H"
if not defined BR_H set "BR_H=(no hash)"
set "BR_H=%BR_H: =%"
echo arm64\%~3-arm64.exe  %BR_H%>> "%INFO%"
exit /b 0

:kit
powershell -NoProfile -ExecutionPolicy Bypass -File "%SRC%\tools\kit\make_kit.ps1" -Port "%~1" -Out "%OUTDIR%\kits\%~2" || (set "STEP=making the build kit for %~2" & exit /b 1)
if not exist "%OUTDIR%\kits\%~2\kit.json" (set "STEP=making the build kit for %~2" & exit /b 1)
exit /b 0

:arm
set CI=1
call "%~1\build_arm64.bat"
if errorlevel 1 (set "ARM_NOTE=%ARM_NOTE% %~3 ARM64 build failed; the rest is fine." & exit /b 0)
copy /y "%~1\out\build\release-arm64\%~2-arm64.exe" "%OUTDIR%\arm64\" >nul
set "BR_H="
for /f "delims=" %%H in ('certutil -hashfile "%OUTDIR%\arm64\%~2-arm64.exe" SHA256 ^| findstr /v ":"') do set "BR_H=%%H"
if not defined BR_H set "BR_H=(no hash)"
set "BR_H=%BR_H: =%"
echo arm64\%~2-arm64.exe  %BR_H%>> "%INFO%"
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
if not defined BR_UNATTENDED pause
exit /b 1
