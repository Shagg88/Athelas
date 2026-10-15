@echo off
rem Starts the ROTK Fix launcher so that The Two Towers boots straight into one level, for testing.
rem The chosen level takes the place of the opening battle: start the game as usual and it loads instead.
rem Only this launch is affected; starting the launcher normally plays the game as usual.
echo.
echo   Two Towers level test
echo.
echo     1  Orthanc (floor reflections)         21ORTH1
echo     2  Balin's Tomb (floor reflections)    04BALIN1
echo     3  Plains of Rohan (water)             16ROHAN1
echo     4  Weathertop                          01WEAT1
echo     5  Gates of Moria                      02GATE2
echo     6  Amon Hen                            13AMON1
echo     7  Fangorn                             14FANG2
echo     8  Helm's Deep                         18HOLE1
echo.
echo   Or type a level code yourself (for example 16ROHAN2).
echo.
set /p pick=Level:
set TT_LEVEL=%pick%
if "%pick%"=="1" set TT_LEVEL=21ORTH1
if "%pick%"=="2" set TT_LEVEL=04BALIN1
if "%pick%"=="3" set TT_LEVEL=16ROHAN1
if "%pick%"=="4" set TT_LEVEL=01WEAT1
if "%pick%"=="5" set TT_LEVEL=02GATE2
if "%pick%"=="6" set TT_LEVEL=13AMON1
if "%pick%"=="7" set TT_LEVEL=14FANG2
if "%pick%"=="8" set TT_LEVEL=18HOLE1
echo Starting with TT_LEVEL=%TT_LEVEL%
start "" /D "C:\Games\ROTK-Test" "C:\Games\ROTK-Test\ROTKFixLauncher.exe"
