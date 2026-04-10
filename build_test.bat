@echo off
REM Build and run the physics test suite (test_vortex.cpp).
REM This compiles the standalone tests against the unchanged physics headers.

setlocal
set PATH=C:\msys64\mingw64\bin;%PATH%

where g++ >nul 2>nul
if errorlevel 1 (
    echo ERROR: g++ not found. Install MSYS2 / MinGW64 and try again.
    exit /b 1
)

echo Building test_vortex.exe ...

g++ -std=c++20 -O2 ^
    test_vortex.cpp ^
    -o test_vortex.exe ^
    -static -static-libgcc -static-libstdc++

if errorlevel 1 (
    echo BUILD FAILED.
    exit /b 1
)

echo.
echo Running tests...
echo.
test_vortex.exe
endlocal
