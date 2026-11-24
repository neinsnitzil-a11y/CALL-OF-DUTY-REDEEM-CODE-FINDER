@echo off
rem ============================================================================
rem  COD Redemption Hunter (C++ / Qt 6) - one-click Windows build
rem    1. Visual Studio 2022 C++ Build Tools   (offers to install with winget)
rem    2. Qt 6 with WebEngine                  (downloads into .qt\ with aqtinstall)
rem    3. builds + tests with CMake
rem    4. collects the app into dist\CODRedemptionHunter\ (windeployqt)
rem    5. compiles the installer with Inno Setup 6 (offers to install with winget)
rem  Result: dist\installer\COD-Redemption-Hunter-Setup-<version>.exe
rem  Use your own Qt:  set HUNTER_QT_DIR=C:\Qt\6.8.3\msvc2022_64  before running.
rem ============================================================================
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title COD Redemption Hunter - Windows build

set "QT_VERSION=6.8.3"
set "QT_ARCH=win64_msvc2022_64"
set "QT_ROOT=%CD%\.qt\%QT_VERSION%\msvc2022_64"
if defined HUNTER_QT_DIR set "QT_ROOT=%HUNTER_QT_DIR%"
set "OUT=%CD%\dist\CODRedemptionHunter"

for /f "tokens=3" %%v in ('findstr /b /c:"project(CODRedemptionHunter" CMakeLists.txt') do set "VER=%%v"
if not defined VER set "VER=0.0.0"
echo.
echo  COD Redemption Hunter %VER% - Windows build
echo  -------------------------------------------

rem ---------------------------------------------------------------- 1. MSVC
echo.
echo [1/5] Visual Studio C++ build tools
call :find_vs
if not defined VSDIR call :get_vs
if not defined VSDIR goto :novs
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :fail
echo       using %VSDIR%
where cmake >nul 2>nul
if errorlevel 1 (
  echo       CMake not found - installing with winget...
  winget install -e --id Kitware.CMake --accept-package-agreements --accept-source-agreements
  set "PATH=%ProgramFiles%\CMake\bin;!PATH!"
)
where cmake >nul 2>nul
if errorlevel 1 goto :nocmake

rem ---------------------------------------------------------------- 2. Qt
echo.
echo [2/5] Qt %QT_VERSION%
if exist "%QT_ROOT%\bin\windeployqt.exe" goto :have_qt
set "PY="
py -3 --version >nul 2>nul
if not errorlevel 1 set "PY=py -3"
if not defined PY (
  python --version >nul 2>nul
  if not errorlevel 1 set "PY=python"
)
if not defined PY goto :nopython
echo       downloading Qt with aqtinstall (about 1.5 GB, one time)...
%PY% -m pip install --upgrade --quiet aqtinstall
if errorlevel 1 goto :fail
%PY% -m aqt install-qt windows desktop %QT_VERSION% %QT_ARCH% -m qtwebengine qtwebchannel qtpositioning --outputdir "%CD%\.qt"
if errorlevel 1 goto :fail
if not exist "%QT_ROOT%\bin\windeployqt.exe" goto :fail
:have_qt
echo       using %QT_ROOT%

rem ---------------------------------------------------------------- 3. build
echo.
echo [3/5] Building
set "GEN=Ninja"
where ninja >nul 2>nul
if errorlevel 1 set "GEN=Visual Studio 17 2022"
cmake -S . -B build -G "%GEN%" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_ROOT%"
if errorlevel 1 goto :fail
cmake --build build --config Release --parallel
if errorlevel 1 goto :fail
ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 echo       WARNING: some core tests failed - see above. Continuing.

rem ---------------------------------------------------------------- 4. collect
echo.
echo [4/5] Collecting the app into dist\CODRedemptionHunter
set "EXE=build\CODRedemptionHunter.exe"
if not exist "%EXE%" set "EXE=build\Release\CODRedemptionHunter.exe"
if not exist "%EXE%" goto :fail
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%\data"
copy /y "%EXE%" "%OUT%\" >nul
copy /y "data\config.json" "%OUT%\data\" >nul
copy /y "data\.env.example" "%OUT%\data\" >nul
copy /y "data\meta.json" "%OUT%\data\" >nul
copy /y "data\research_sources.json" "%OUT%\data\" >nul
mkdir "%OUT%\data\i18n"
copy /y "data\i18n\*.json" "%OUT%\data\i18n\" >nul
copy /y README.md "%OUT%\" >nul
copy /y LICENSE "%OUT%\" >nul
"%QT_ROOT%\bin\windeployqt.exe" --release --no-translations --no-compiler-runtime --no-opengl-sw "%OUT%\CODRedemptionHunter.exe"
if errorlevel 1 goto :fail
rem app-local Visual C++ runtime, so nobody needs to install vc_redist
if defined VCToolsRedistDir (
  for /d %%d in ("%VCToolsRedistDir%x64\Microsoft.VC*.CRT") do copy /y "%%d\*.dll" "%OUT%\" >nul
)
echo       ok

rem ---------------------------------------------------------------- 5. installer
echo.
echo [5/5] Installer (Inno Setup 6)
call :find_iscc
if not defined ISCC call :get_inno
if not defined ISCC goto :noinno
"%ISCC%" /Qp "/DAppVersion=%VER%" packaging\installer.iss
if errorlevel 1 goto :fail
echo.
echo  Done:  dist\installer\COD-Redemption-Hunter-Setup-%VER%.exe
echo  (the portable app is in dist\CODRedemptionHunter\)
start "" explorer "%CD%\dist\installer"
pause
exit /b 0

rem ============================================================ helpers
:find_vs
set "VSDIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 0
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
exit /b 0

:get_vs
where winget >nul 2>nul
if errorlevel 1 exit /b 0
echo       Visual Studio 2022 C++ Build Tools are needed (about 3 GB, free).
choice /c YN /m "      Install them now with winget"
if errorlevel 2 exit /b 0
winget install -e --id Microsoft.VisualStudio.2022.BuildTools --accept-package-agreements --accept-source-agreements --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.CMake.Project --includeRecommended"
call :find_vs
exit /b 0

:find_iscc
set "ISCC="
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC for /f "delims=" %%i in ('where ISCC.exe 2^>nul') do set "ISCC=%%i"
exit /b 0

:get_inno
where winget >nul 2>nul
if errorlevel 1 exit /b 0
echo       Inno Setup 6 not found - installing with winget...
winget install -e --id JRSoftware.InnoSetup --accept-package-agreements --accept-source-agreements
call :find_iscc
exit /b 0

:novs
echo.
echo  Visual Studio 2022 C++ Build Tools are required.
echo  Get them free: https://visualstudio.microsoft.com/visual-cpp-build-tools/
echo  (tick "Desktop development with C++"), then run this again.
goto :end_fail
:nocmake
echo.
echo  CMake is required: https://cmake.org/download/  (or: winget install Kitware.CMake)
goto :end_fail
:nopython
echo.
echo  Python 3 is needed once, to download Qt (aqtinstall). Install it from https://www.python.org/
echo  or: winget install Python.Python.3.12  -- or install Qt yourself and set HUNTER_QT_DIR.
goto :end_fail
:noinno
echo.
echo  Inno Setup 6 is needed for the installer: https://jrsoftware.org/isdl.php
echo  The app itself is ready in dist\CODRedemptionHunter\
goto :end_fail
:fail
echo.
echo  Build failed - see the messages above.
:end_fail
pause
exit /b 1
