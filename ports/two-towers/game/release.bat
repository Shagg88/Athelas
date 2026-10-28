@echo off
set "GAME_EXE=TwoTowers"
cd /d "%~dp0"
set "OUT=out\build\release"
rem build first, so the copy is never older than the source (CI: no questions, no "play now")
set CI=1
call build.bat || exit /b 1
rem the configure step writes where the SDK is
if not exist "%OUT%\sdk_dir.txt" cmake --preset release >nul || exit /b 1
if not exist "%OUT%\sdk_dir.txt" echo %OUT%\sdk_dir.txt was not written, update the SDK. && exit /b 1
set /p SDK=<out\build\release\sdk_dir.txt
rem Athelas's layout: the folder it ships as TT-Xbox, holding only the exe and the licences
rd /s /q "%OUT%\TwoTowers" 2>nul
mkdir "%OUT%\TwoTowers"
copy /y "%OUT%\%GAME_EXE%.exe" "%OUT%\TwoTowers\" >nul || exit /b 1
copy /y "%SDK%\THIRD_PARTY.txt" "%OUT%\TwoTowers\" >nul || exit /b 1
copy /y "%SDK%\LICENSE" "%OUT%\TwoTowers\LICENSE-XboxXBE-Recomp.txt" >nul || exit /b 1
rem the subtitles' translations (our own text, lang\<code>.txt)
if exist lang xcopy /e /i /q /y lang "%OUT%\TwoTowers\lang" >nul || exit /b 1
rem the exe's SHA-256, for the release notes
certutil -hashfile "%OUT%\TwoTowers\%GAME_EXE%.exe" SHA256 | findstr /v ":" > "%OUT%\TwoTowers\%GAME_EXE%.exe.sha256"
echo Launcher folder: %CD%\%OUT%\TwoTowers
rem the debugging symbols for this exact build, kept (not shipped) to read players' crash dumps: symbols\<exe sha256>
set /p HASH=<"%OUT%\TwoTowers\%GAME_EXE%.exe.sha256"
set "HASH=%HASH: =%"
if not exist "symbols\%HASH%" mkdir "symbols\%HASH%"
copy /y "%OUT%\%GAME_EXE%.pdb" "symbols\%HASH%\" >nul
copy /y "%OUT%\%GAME_EXE%.exe" "symbols\%HASH%\" >nul
echo Symbols kept in: %CD%\symbols\%HASH%
rem the zip for playing without the launcher: the exe, its licences and readme, and an empty assets folder where
rem the game (files or .iso) goes. tar is part of Windows 10 and 11.
del /q "%GAME_EXE%.zip" 2>nul
rd /s /q "%OUT%\release" 2>nul
mkdir "%OUT%\release\assets"
copy /y "%OUT%\TwoTowers\*" "%OUT%\release\" >nul || exit /b 1
copy /y README.md "%OUT%\release\" >nul || exit /b 1
if exist lang xcopy /e /i /q /y lang "%OUT%\release\lang" >nul || exit /b 1
tar -a -c -f "%GAME_EXE%.zip" -C "%OUT%\release" %GAME_EXE%.exe %GAME_EXE%.exe.sha256 THIRD_PARTY.txt LICENSE-XboxXBE-Recomp.txt README.md assets lang || exit /b 1
echo Zip: %CD%\%GAME_EXE%.zip
