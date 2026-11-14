@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0.."
set PATH=C:\msys64\mingw64\bin;%PATH%
set "STAGE=.build\win"
set "OUT=dist\win"
if not exist "%STAGE%" mkdir "%STAGE%"
set CFLAGS=-std=c11 -O2 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -Icore -Icore/include
set CORE=core/base.c core/net.c core/tunnel.c core/curve25519-donna.c core/qcompress.c

gcc -shared %CFLAGS% -o "%STAGE%\fairlead-core.dll" %CORE% core/abi.c -Wl,-Bstatic -lz -Wl,-Bdynamic -lws2_32 -lbcrypt > "%STAGE%\build.log" 2>&1
if errorlevel 1 goto done
gcc %CFLAGS% -o "%STAGE%\fairlead-helper.exe" %CORE% platform/win/win_tun.c platform/win/win_helper.c platform/win/win_main.c -mwindows -municode -Wl,-Bstatic -lz -Wl,-Bdynamic -lws2_32 -lbcrypt -liphlpapi -ladvapi32 -lpsapi >> "%STAGE%\build.log" 2>&1
if errorlevel 1 goto done

if not exist "%STAGE%\amneziawg-go.exe" if exist "%OUT%\bin\amneziawg-go.exe" copy /y "%OUT%\bin\amneziawg-go.exe" "%STAGE%\" >nul
if not exist "%STAGE%\amneziawg-go.exe" if exist "%OUT%\amneziawg-go.exe" copy /y "%OUT%\amneziawg-go.exe" "%STAGE%\" >nul
if not exist "%STAGE%\wintun.dll" if exist "%OUT%\bin\wintun.dll" copy /y "%OUT%\bin\wintun.dll" "%STAGE%\" >nul
if not exist "%STAGE%\wintun.dll" if exist "%OUT%\wintun.dll" copy /y "%OUT%\wintun.dll" "%STAGE%\" >nul
if not exist "%STAGE%\wintun.dll" if exist ".build\win-userspace\wintun\wintun\bin\amd64\wintun.dll" copy /y ".build\win-userspace\wintun\wintun\bin\amd64\wintun.dll" "%STAGE%\" >nul

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo vswhere was not found>> "%STAGE%\build.log"
  goto done
)
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL (
  echo Visual Studio C++ tools were not found>> "%STAGE%\build.log"
  goto done
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 goto done
MSBuild.exe platform\win\app\Fairlead.vcxproj /restore /p:Configuration=Release /p:Platform=x64 /p:RestoreProjectStyle=PackageReference /p:RestorePackagesPath=%USERPROFILE%\.nuget\packages /m /v:minimal >> "%STAGE%\build.log" 2>&1
if errorlevel 1 goto done
if not exist "%OUT%\bin" mkdir "%OUT%\bin"
powershell -NoProfile -Command "Get-ChildItem -LiteralPath 'dist\win\bin' -File | Where-Object { $_.Name -match '^(Microsoft\.Web\.WebView2\.Core\.Projection\.dll$|onnxruntime|DirectML|Microsoft\.Windows\.AI\.|Microsoft\.Windows\.Widgets|Microsoft\.Graphics\.Imaging|workloads)' -or $_.Extension -match '^\.(pdb|ilk|winmd|iobj|ipdb)$' } | Remove-Item -Force" >> "%STAGE%\build.log" 2>&1
call :ship "%STAGE%\fairlead-core.dll" "%OUT%\bin\fairlead-core.dll"
call :ship "%STAGE%\fairlead-helper.exe" "%OUT%\fairlead-helper.exe"
if exist "%STAGE%\amneziawg-go.exe" call :ship "%STAGE%\amneziawg-go.exe" "%OUT%\amneziawg-go.exe"
if exist "%STAGE%\wintun.dll" call :ship "%STAGE%\wintun.dll" "%OUT%\wintun.dll"
windres platform\win\win_launch.rc -O coff -o "%STAGE%\win_launch.res" >> "%STAGE%\build.log" 2>&1
if exist "%STAGE%\win_launch.res" (
  gcc -o "%OUT%\Fairlead.exe" platform\win\win_launch.c "%STAGE%\win_launch.res" -mwindows -municode >> "%STAGE%\build.log" 2>&1
) else (
  gcc -o "%OUT%\Fairlead.exe" platform\win\win_launch.c -mwindows -municode >> "%STAGE%\build.log" 2>&1
)
if errorlevel 1 echo Could not write %OUT%\Fairlead.exe. Quit Fairlead from the tray and build again.
:done
echo EXIT:!ERRORLEVEL!>> "%STAGE%\build.log"
endlocal
goto :eof

:ship
copy /y "%~1" "%~2" >nul
if not errorlevel 1 exit /b 0
echo %~2 is in use. Quit Fairlead from the tray. fairlead-helper.exe keeps the tunnel files open too.
exit /b 1
