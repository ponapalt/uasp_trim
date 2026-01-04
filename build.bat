@echo off
REM UASP SSD Full TRIM Tool - Build Script
REM This script automatically sets up VS2022 environment and builds the tool.

setlocal

REM Find VS2022 installation
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Error: Visual Studio 2022 not found.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Workload.NativeDesktop -property installationPath`) do set "VSINSTALL=%%i"

if not defined VSINSTALL (
    echo Error: Visual Studio 2022 with C++ workload not found.
    exit /b 1
)

REM Setup environment
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    call "%VSINSTALL%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
)

REM Build
echo Building UASP SSD Full TRIM Tool...
echo.

rc /nologo uasp_trim.rc
if errorlevel 1 goto :error

cl /nologo /EHsc /W4 /O2 /DNDEBUG main.cpp uasp_trim.res /Fe:uasp_trim.exe /link /nologo advapi32.lib
if errorlevel 1 goto :error

echo.
echo Build successful: uasp_trim.exe
goto :end

:error
echo.
echo Build failed!
exit /b 1

:end
endlocal
