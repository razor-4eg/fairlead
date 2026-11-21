@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0.."
set PATH=C:\msys64\mingw64\bin;%PATH%
set "STAGE=.build\win"
set "OUT=dist\win"
if not exist "%STAGE%" mkdir "%STAGE%"
set CFLAGS=-std=c11 -O2 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -Icore -Icore/include
set CORE=core/base.c core/net.c core/tunnel.c core/curve25519-donna.c core/qcompress.c core/mihomo.c

gcc -shared %CFLAGS% -o "%STAGE%\fairlead-core.dll" %CORE% core/abi.c -Wl,-Bstatic -lz -Wl,-Bdynamic -lws2_32 -lbcrypt > "%STAGE%\build.log" 2>&1
if errorlevel 1 goto fail
gcc %CFLAGS% -o "%STAGE%\fairlead-helper.exe" %CORE% platform/win/win_tun.c platform/win/win_helper.c platform/win/win_main.c -mwindows -municode -Wl,-Bstatic -lz -Wl,-Bdynamic -lws2_32 -lbcrypt -lwinhttp -liphlpapi -ladvapi32 -lpsapi >> "%STAGE%\build.log" 2>&1
if errorlevel 1 goto fail

if not exist "%STAGE%\mihomo.exe" if exist "%OUT%\bin\mihomo.exe" copy /y "%OUT%\bin\mihomo.exe" "%STAGE%\" >nul
if not exist "%STAGE%\wintun.dll" if exist "%OUT%\bin\wintun.dll" copy /y "%OUT%\bin\wintun.dll" "%STAGE%\" >nul
if not exist "%STAGE%\wintun.dll" if exist ".build\win-userspace\wintun\wintun\bin\amd64\wintun.dll" copy /y ".build\win-userspace\wintun\wintun\bin\amd64\wintun.dll" "%STAGE%\" >nul

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo vswhere was not found>> "%STAGE%\build.log"
  goto fail
)
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL (
  echo Visual Studio C++ tools were not found>> "%STAGE%\build.log"
  goto fail
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 goto fail
MSBuild.exe platform\win\app\Fairlead.vcxproj /restore /p:Configuration=Release /p:Platform=x64 /p:RestoreProjectStyle=PackageReference /p:RestorePackagesPath=%USERPROFILE%\.nuget\packages /m /v:minimal >> "%STAGE%\build.log" 2>&1
if errorlevel 1 goto fail
if not exist "%OUT%\bin" mkdir "%OUT%\bin"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0clean-win-bin.ps1" >> "%STAGE%\build.log" 2>&1
call :ship "%STAGE%\fairlead-core.dll" "%OUT%\bin\fairlead-core.dll"
if errorlevel 1 goto fail
call :ship "%STAGE%\fairlead-helper.exe" "%OUT%\fairlead-helper.exe"
if errorlevel 1 goto fail
if exist "%STAGE%\mihomo.exe" call :ship "%STAGE%\mihomo.exe" "%OUT%\bin\mihomo.exe"
if errorlevel 1 goto fail
if exist "%STAGE%\wintun.dll" call :ship "%STAGE%\wintun.dll" "%OUT%\bin\wintun.dll"
if errorlevel 1 goto fail
windres platform\win\win_launch.rc -O coff -o "%STAGE%\win_launch.res" >> "%STAGE%\build.log" 2>&1
if exist "%STAGE%\win_launch.res" (
  gcc -o "%OUT%\Fairlead.exe" platform\win\win_launch.c "%STAGE%\win_launch.res" -mwindows -municode >> "%STAGE%\build.log" 2>&1
) else (
  gcc -o "%OUT%\Fairlead.exe" platform\win\win_launch.c -mwindows -municode >> "%STAGE%\build.log" 2>&1
)
if errorlevel 1 (
  echo Could not write %OUT%\Fairlead.exe. Quit Fairlead from the tray and build again.
  goto fail
)
echo Built %OUT%\Fairlead.exe
echo EXIT:0>> "%STAGE%\build.log"
endlocal
exit /b 0

:fail
echo Build failed. See %STAGE%\build.log
type "%STAGE%\build.log"
echo EXIT:1>> "%STAGE%\build.log"
endlocal
exit /b 1

:ship
copy /y "%~1" "%~2" >nul
if not errorlevel 1 exit /b 0
echo %~2 is in use. Quit Fairlead from the tray. fairlead-helper.exe keeps the tunnel files open too.
exit /b 1
