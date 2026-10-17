@echo off
setlocal
rem build_arm64.bat: %GAME_EXE%-arm64.exe for Windows on ARM PCs (Snapdragon laptops), built on this x64 PC.
rem Run build.bat first: the ARM64 build uses its recompiler and its generated code.
set "GAME_TITLE=ROTK Xbox PC port"
set "GAME_EXE=ROTKXbox"
title %GAME_TITLE% - ARM64 build
cd /d "%~dp0"
echo.
echo   %GAME_TITLE% - build for Windows on ARM
echo   ================================================
echo.
set "PF86=%ProgramFiles(x86)%"
set "VSINST=%PF86%\Microsoft Visual Studio\Installer"
set "VSWHERE=%VSINST%\vswhere.exe"
if not exist out\build\release\xbe_recomp.exe (
    echo   Run build.bat first: this build uses its recompiler.
    goto :fail
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo   Visual Studio with the C++ workload was not found.
    goto :fail
)
rem the ARM64 libraries: "MSVC Build Tools for ARM64/ARM64EC" in the Visual Studio Installer
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=arm64 -host_arch=amd64 >nul
if not exist "%VCToolsInstallDir%lib\arm64\libcmt.lib" (
    echo   Visual Studio's ARM64 libraries are missing.
    if defined CI goto :fail
    choice /c YN /n /m "  Let Visual Studio install them? [Y/N] "
    if errorlevel 2 goto :byhand
    echo   Installing. Accept the admin prompt, then wait for the installer.
    powershell -NoProfile -Command "Start-Process -Verb RunAs -Wait -FilePath '%VSINST%\setup.exe' -ArgumentList 'modify','--installPath','%VSDIR%','--add','Microsoft.VisualStudio.Component.VC.Tools.ARM64','--passive','--norestart','--force'"
    echo   Press a key once the Visual Studio Installer window has closed.
    pause >nul
    call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=arm64 -host_arch=amd64 >nul
    if not exist "%VCToolsInstallDir%lib\arm64\libcmt.lib" goto :byhand
)
set "VSCMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "PATH=%VSCMAKE%\CMake\bin;%VSCMAKE%\Ninja;%VSDIR%\VC\Tools\Llvm\x64\bin;%PATH%"
if exist "%ProgramFiles%\LLVM\bin\clang++.exe" set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"

echo   Compiling for ARM64 (the first time takes as long as build.bat's did) ...
cmake --preset release-arm64 >nul || goto :fail
cmake --build out\build\release-arm64 --target %GAME_EXE% || goto :fail
copy /y out\build\release-arm64\%GAME_EXE%.exe out\build\release-arm64\%GAME_EXE%-arm64.exe >nul || goto :fail
echo.
echo   Built out\build\release-arm64\%GAME_EXE%-arm64.exe
echo   Put it beside %GAME_EXE%.exe: Athelas starts it on Windows on ARM PCs.
echo.
if not defined CI pause
exit /b 0

:byhand
echo   In the Visual Studio Installer: Modify, Individual components, tick
echo   "MSVC Build Tools for ARM64/ARM64EC (Latest)", then run this again.
:fail
echo.
echo   BUILD FAILED. Read the last message above.
echo.
if not defined CI pause
exit /b 1
