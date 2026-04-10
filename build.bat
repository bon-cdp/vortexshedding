@echo off
REM Torsor v0.2 - Windows build script
REM Builds torsor.exe (the GUI app) using MinGW64 g++ from MSYS2.
REM
REM The resulting torsor.exe is statically linked - it can be copied to any
REM Windows machine and run without installing MSYS2 or MinGW runtime DLLs.

setlocal

REM Add MinGW64 to PATH if not already there
set PATH=C:\msys64\mingw64\bin;%PATH%

where g++ >nul 2>nul
if errorlevel 1 (
    echo ERROR: g++ not found. Install MSYS2 / MinGW64 and try again.
    exit /b 1
)

echo Building torsor.exe ...

g++ -std=c++20 -O2 -mwindows ^
    torsor_win.cpp ^
    -o torsor.exe ^
    -lgdi32 -lgdiplus -lcomctl32 -lcomdlg32 -lole32 -lshlwapi ^
    -static -static-libgcc -static-libstdc++ ^
    -Wl,--subsystem,windows

if errorlevel 1 (
    echo.
    echo BUILD FAILED.
    exit /b 1
)

echo.
echo BUILD OK -^> torsor.exe
dir torsor.exe | findstr torsor.exe
endlocal
