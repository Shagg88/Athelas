@echo off
rem UPDATE_SOURCE.bat - brings C:\Athelas\source up to date with backup\athelas.bundle, the latest Claude has put
rem there. Only moves forward (fast-forward): it stops instead of overwriting anything changed in source\ by hand.
setlocal EnableExtensions
for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
set "SRC=%ROOT%\source"
set "BUNDLE=%ROOT%\backup\athelas.bundle"
where git >nul 2>nul || for /d %%V in ("%ProgramFiles%\Microsoft Visual Studio\*") do for /d %%E in ("%%V\*") do (
  if exist "%%E\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd\git.exe" set "PATH=%%E\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd;%PATH%"
)
rem No bundle: on its own that is fine (exit 0, nothing changes); a release build ("quiet") must not go on with an
rem unknown checkout, so it gets exit code 2 and stops.
if not exist "%BUNDLE%" (echo No backup\athelas.bundle: source\ stays as it is. & if /i "%~1"=="quiet" (exit /b 2) else (pause & exit /b 0))
git -C "%SRC%" fetch -q "%BUNDLE%" "+refs/heads/*:refs/remotes/claude/*" "+refs/tags/*:refs/tags/*" || (echo Could not read %BUNDLE%. & goto :end_fail)
git -C "%SRC%" checkout -q main || goto :end_fail
git -C "%SRC%" merge -q --ff-only claude/main || (echo source\ has changes the backup doesn't: nothing was overwritten. Tell Claude. & goto :end_fail)
for /f "delims=" %%C in ('git -C "%SRC%" log -1 "--format=%%h %%s"') do echo source\ is at %%C
if /i not "%~1"=="quiet" pause
exit /b 0
:end_fail
if /i not "%~1"=="quiet" pause
exit /b 1
