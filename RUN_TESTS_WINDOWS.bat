@echo off
setlocal
title DistriArch - Native C++20 Tests
pushd "%~dp0"
if errorlevel 1 exit /b 1
"build\distriarch_tests.exe" --all
set "DISTRIARCH_EXIT=%ERRORLEVEL%"
echo.
pause
popd
exit /b %DISTRIARCH_EXIT%
