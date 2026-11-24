@echo off
rem Build the Android APK from Windows using WSL (Ubuntu). Install WSL first:  wsl --install -d Ubuntu
setlocal
cd /d "%~dp0"
where wsl >nul 2>nul || (echo WSL is not installed. Run in an admin terminal:  wsl --install -d Ubuntu   then reboot and run this again. & pause & exit /b 1)
for /f "usebackq delims=" %%P in (`wsl wslpath -a "%CD%"`) do set "WSLDIR=%%P"
echo Building in WSL: %WSLDIR%
wsl bash -lc "cd '%WSLDIR%' && export HUNTER_ROOT=$(pwd) && sed 's/\r$//' build_android.sh > /tmp/hunter_android.sh && bash /tmp/hunter_android.sh %*"
if errorlevel 1 (echo. & echo Android build failed - see above. & pause & exit /b 1)
echo. & echo APK is in the dist folder.
pause
