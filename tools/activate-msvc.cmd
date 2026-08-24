@echo off
rem Locate the newest Visual Studio installation that has the x64 C++
rem toolchain. GitHub's windows-latest image can move between VS releases,
rem so workflows must not hard-code a year or edition in this path.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere.exe was not found at "%VSWHERE%". 1>&2
  exit /b 1
)

set "CHRONOID_VS_INSTALL="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CHRONOID_VS_INSTALL=%%i"
if not defined CHRONOID_VS_INSTALL (
  echo ERROR: no Visual Studio installation with the x64 C++ toolchain was found. 1>&2
  exit /b 1
)

call "%CHRONOID_VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat"
