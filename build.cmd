@echo off
setlocal

rem Builds build\SamsungBrightness.exe (Release, x64) with the Visual Studio C++ tools.
rem Requires Visual Studio 2022 or newer with the "Desktop development with C++" workload.

rem vcvars needs the standard Windows tools (findstr, reg) and vswhere on PATH.
set "VS_INSTALLER=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
set "PATH=%SystemRoot%\System32;%SystemRoot%;%VS_INSTALLER%;%PATH%"

set "VSWHERE=%VS_INSTALLER%\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"

if not defined VS_PATH (
    echo Visual Studio with the "Desktop development with C++" workload was not found.
    exit /b 1
)

call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%~dp0build" || exit /b 1

echo.
echo Built: %~dp0build\SamsungBrightness.exe
