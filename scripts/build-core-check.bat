@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0.."
set PATH=C:\msys64\mingw64\bin;%PATH%
if not exist .build\win mkdir .build\win
gcc -std=c11 -Wall -Wno-unused-function -Wno-unused-variable -O2 -Icore -Icore/include -o .build/win/core_check.exe core/fl.c core/fl_ip.c core/fl_doc.c core/fl_keys.c core/fl_dns.c core/fl_bypass.c core/fl_uapi.c core/fl_json.c core/fl_amnezia.c core/curve25519-donna.c core/qcompress.c tests/core_check.c -lws2_32 -lbcrypt -lz > .build\win\core-check.log 2>&1
echo EXIT:!ERRORLEVEL!>> .build\win\core-check.log
