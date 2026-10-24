@echo off
setlocal
title DistriArch - C++20 Architecture Simulator
pushd "%~dp0"
if errorlevel 1 exit /b 1
if not exist "build\distriarch.exe" (
    echo Please extract the complete ZIP before starting the project.
    pause
    popd
    exit /b 1
)
echo.
echo DistriArch - native C++20 simulator
echo Open this address in your browser: http://127.0.0.1:8000
echo Keep this window open. Press Ctrl+C to stop the server.
echo No Python, Node.js, compiler or internet connection is needed to run.
echo.
"build\distriarch.exe" serve --root "." --port 8000
set "DISTRIARCH_EXIT=%ERRORLEVEL%"
if not "%DISTRIARCH_EXIT%"=="0" (
    echo.
    echo If port 8000 is already in use, stop the other project server,
    echo or run: build\distriarch.exe serve --root . --port 8010
    pause
)
popd
exit /b %DISTRIARCH_EXIT%
