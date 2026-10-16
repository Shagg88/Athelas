@echo off
rem A release copy for Athelas: release\ROTKXbox\ with only ROTKXbox.exe, LICENSE.txt (the recompiler runtime's
rem MIT licence), THIRD_PARTY.txt (the licences of the libraries built into it; both must ship with it) and
rem ROTKXbox.exe.sha256. Athelas ships it as its ROTK-Xbox folder.
rem The game files, settings, saves, logs and shader cache all live where the launcher's arguments say.
setlocal
set "GAME_EXE=ROTKXbox"
cd /d "%~dp0"
set "OUT=out\build\release"
rem build first, so the copy is never older than the source (CI: no questions, no "play now")
set CI=1
call build.bat || exit /b 1
if not exist "%OUT%\%GAME_EXE%.exe" echo %OUT%\%GAME_EXE%.exe was not built. && exit /b 1
rem the configure step writes where the SDK is
if not exist "%OUT%\sdk_dir.txt" cmake --preset release >nul || exit /b 1
if not exist "%OUT%\sdk_dir.txt" echo %OUT%\sdk_dir.txt was not written, update the SDK. && exit /b 1
set /p SDK=<"%OUT%\sdk_dir.txt"
rd /s /q "release\%GAME_EXE%" 2>nul
mkdir "release\%GAME_EXE%" || exit /b 1
copy /y "%OUT%\%GAME_EXE%.exe" "release\%GAME_EXE%\" >nul || exit /b 1
copy /y "%SDK%\THIRD_PARTY.txt" "release\%GAME_EXE%\" >nul || exit /b 1
copy /y "%SDK%\LICENSE" "release\%GAME_EXE%\LICENSE.txt" >nul || exit /b 1
rem the exe's SHA-256 as sha256sum writes it: "<hash>  ROTKXbox.exe"
powershell -NoProfile -Command "$h = (Get-FileHash -Algorithm SHA256 'release\%GAME_EXE%\%GAME_EXE%.exe').Hash.ToLower(); [IO.File]::WriteAllText('release\%GAME_EXE%\%GAME_EXE%.exe.sha256', $h + '  %GAME_EXE%.exe' + [char]10)" || exit /b 1
echo.
echo Release copy: %CD%\release\%GAME_EXE%
dir /b "release\%GAME_EXE%"
rem a zip of the folder as well, when 7-Zip is installed
where 7z >nul 2>nul && (del /q "release\%GAME_EXE%.zip" 2>nul & 7z a "release\%GAME_EXE%.zip" ".\release\%GAME_EXE%" >nul && echo Zip: %CD%\release\%GAME_EXE%.zip)
exit /b 0
