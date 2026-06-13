@echo off
cd /d D:\OpenClaw\workspace\process-guardian
echo Building guardian.exe...
gcc -Wall -Wextra -O2 -o guardian.exe main.c src/gui/gui.c src/core/process_monitor.c src/core/process_protector.c src/utils/logger.c -lcomctl32 -lpsapi -lshell32 -luser32 -lkernel32
if errorlevel 1 (
    echo Build failed!
    exit /b 1
)
echo Building guardiand.exe...
gcc -Wall -Wextra -O2 -o guardiand.exe daemon.c -lpsapi -lkernel32 -luser32
if errorlevel 1 (
    echo Daemon build failed!
    exit /b 1
)
echo All builds successful!