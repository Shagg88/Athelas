@echo off
rem Testing only: starts the game straight in a chosen level instead of the prologue.
rem The rest is like capture.bat (F3 captures, timing log). Quit and restart to pick another level.
set "XBE_CAPTURE_DIR=%~dp0captures"
set XBE_PERF=1
if not exist "%XBE_CAPTURE_DIR%" mkdir "%XBE_CAPTURE_DIR%"
echo.
echo  Start in which level?
echo.
echo   1  01WEAT1   Weathertop           8  16ROHAN3  Rohan, third level
echo   2  02GATE2   Caradhras            9  17TOP1    Helm's Deep
echo   3  04BALIN1  Moria               10  18HOLE1   Helm's Deep
echo   4  13AMON1   Amon Hen            11  20YARD1   Helm's Deep courtyard
echo   5  14FANG2   Fangorn             12  21ORTH1   Orthanc tower
echo   6  16ROHAN1  Rohan, first level  13  21ORTH2   Orthanc top
echo   7  16ROHAN2  Rohan, second level
echo.
set /p PICK=Number: 
set TT_LEVEL=
if "%PICK%"=="1" set TT_LEVEL=01WEAT1
if "%PICK%"=="2" set TT_LEVEL=02GATE2
if "%PICK%"=="3" set TT_LEVEL=04BALIN1
if "%PICK%"=="4" set TT_LEVEL=13AMON1
if "%PICK%"=="5" set TT_LEVEL=14FANG2
if "%PICK%"=="6" set TT_LEVEL=16ROHAN1
if "%PICK%"=="7" set TT_LEVEL=16ROHAN2
if "%PICK%"=="8" set TT_LEVEL=16ROHAN3
if "%PICK%"=="9" set TT_LEVEL=17TOP1
if "%PICK%"=="10" set TT_LEVEL=18HOLE1
if "%PICK%"=="11" set TT_LEVEL=20YARD1
if "%PICK%"=="12" set TT_LEVEL=21ORTH1
if "%PICK%"=="13" set TT_LEVEL=21ORTH2
if "%TT_LEVEL%"=="" (
    echo Not a level number.
    pause
    exit /b
)
cd /d "%~dp0out\build\release"
start "" TwoTowers.exe
