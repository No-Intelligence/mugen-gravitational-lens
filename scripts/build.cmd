@echo off
setlocal
cd /d "%~dp0.."
if not defined VSCMD_VER (
  set "LENS_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    echo Install Visual Studio Build Tools with Desktop development with C++.
    exit /b 1
  )
  for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "LENS_VS=%%I"
  call :setup
  if errorlevel 1 exit /b 1
)
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x86" (
  echo Use an x86 Native Tools prompt.
  exit /b 1
)
if not exist build mkdir build
cl /nologo /std:c++17 /utf-8 /W4 /WX /O2 /MT /EHsc /LD src\math\math.cpp /Fobuild\math.obj /Febuild\mugen-lens-math.dll /link /DEF:src\math\math.def /IMPLIB:build\math.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /W4 /WX /O2 /MT /EHsc /LD src\adapter\mod.cpp build\math.lib /Fobuild\mod.obj /Febuild\mod.dll /link /DEF:src\adapter\mod.def /IMPLIB:build\mod.lib bcrypt.lib winmm.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /W4 /WX /O2 /MT /EHsc tests\native\lens_math_test.cpp build\math.lib /Fobuild\math_test.obj /Febuild\lens-math-test.exe
exit /b %errorlevel%
:setup
if not defined LENS_VS exit /b 1
call "%LENS_VS%\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
exit /b %errorlevel%
