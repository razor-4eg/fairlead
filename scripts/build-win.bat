@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0.."
set PATH=C:\msys64\mingw64\bin;%PATH%
set "STAGE=.build\win"
set "OUT=dist\win"
if not exist "%STAGE%" mkdir "%STAGE%"
set CFLAGS=-std=c11 -O2 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -Icore -Icore/include
set CORE=core/fl.c core/fl_ip.c core/fl_doc.c core/fl_keys.c core/fl_dns.c core/fl_bypass.c core/fl_uapi.c core/fl_json.c core/fl_amnezia.c core/curve25519-donna.c core/qcompress.c

gcc -shared %CFLAGS% -o "%STAGE%\fairlead-core.dll" %CORE% core/fl_abi.c -Wl,-Bstatic -lz -Wl,-Bdynamic -lws2_32 -lbcrypt > "%STAGE%\build.log" 2>&1
if errorlevel 1 goto done
gcc %CFLAGS% -o "%STAGE%\fairlead-helper.exe" %CORE% platform/win/win_tun.c platform/win/win_helper.c platform/win/win_main.c -mwindows -municode -Wl,-Bstatic -lz -Wl,-Bdynamic -lws2_32 -lbcrypt -liphlpapi -ladvapi32 >> "%STAGE%\build.log" 2>&1
if errorlevel 1 goto done

where dotnet >nul 2>&1
if errorlevel 1 (
  echo dotnet SDK is not installed>> "%STAGE%\build.log"
  goto done
)

if not exist "%STAGE%\amneziawg-go.exe" if exist "%OUT%\bin\amneziawg-go.exe" copy /y "%OUT%\bin\amneziawg-go.exe" "%STAGE%\" >nul
if not exist "%STAGE%\amneziawg-go.exe" if exist "%OUT%\amneziawg-go.exe" copy /y "%OUT%\amneziawg-go.exe" "%STAGE%\" >nul
if not exist "%STAGE%\wintun.dll" if exist "%OUT%\bin\wintun.dll" copy /y "%OUT%\bin\wintun.dll" "%STAGE%\" >nul
if not exist "%STAGE%\wintun.dll" if exist "%OUT%\wintun.dll" copy /y "%OUT%\wintun.dll" "%STAGE%\" >nul
if not exist "%STAGE%\wintun.dll" if exist ".build\win-userspace\wintun\wintun\bin\amd64\wintun.dll" copy /y ".build\win-userspace\wintun\wintun\bin\amd64\wintun.dll" "%STAGE%\" >nul

if exist "%STAGE%\publish" rmdir /s /q "%STAGE%\publish"
dotnet publish platform\win\ui\Fairlead.csproj -c Release -r win-x64 --self-contained true -o "%STAGE%\publish" >> "%STAGE%\build.log" 2>&1
if errorlevel 1 goto done

if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%\bin"
xcopy "%STAGE%\publish" "%OUT%\bin" /E /I /Q /Y >nul
if errorlevel 1 goto done
copy /y "%STAGE%\fairlead-core.dll" "%OUT%\bin\" >nul
copy /y "%STAGE%\fairlead-helper.exe" "%OUT%\" >nul
if exist "%STAGE%\amneziawg-go.exe" copy /y "%STAGE%\amneziawg-go.exe" "%OUT%\" >nul
if exist "%STAGE%\wintun.dll" copy /y "%STAGE%\wintun.dll" "%OUT%\" >nul
windres platform\win\win_launch.rc -O coff -o "%STAGE%\win_launch.res" >> "%STAGE%\build.log" 2>&1
if exist "%STAGE%\win_launch.res" (
  gcc -o "%OUT%\Fairlead.exe" platform\win\win_launch.c "%STAGE%\win_launch.res" -mwindows -municode >> "%STAGE%\build.log" 2>&1
) else (
  gcc -o "%OUT%\Fairlead.exe" platform\win\win_launch.c -mwindows -municode >> "%STAGE%\build.log" 2>&1
)
if errorlevel 1 goto done
:done
echo EXIT:!ERRORLEVEL!>> "%STAGE%\build.log"
endlocal
