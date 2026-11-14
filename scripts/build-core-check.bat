@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0.."
set PATH=C:\msys64\mingw64\bin;%PATH%
if not exist .build\win mkdir .build\win
gcc -std=c11 -Wall -Wno-unused-function -Wno-unused-variable -O2 -Icore -Icore/include -o .build/win/core_check.exe core/base.c core/net.c core/tunnel.c core/curve25519-donna.c core/qcompress.c tests/core_check.c -lws2_32 -lbcrypt -lz > .build\win\core-check.log 2>&1
echo EXIT:!ERRORLEVEL!>> .build\win\core-check.log
