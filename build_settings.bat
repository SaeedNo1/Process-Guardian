@echo off
cd /d D:\OpenClaw\workspace\process-guardian
gcc -Wall -Wextra -O2 -o settings.exe settings.c -lkernel32 -luser32
if errorlevel 1 (
    echo Build failed!
) else (
    echo Build success: settings.exe
)