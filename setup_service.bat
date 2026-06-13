@echo off
cd /d D:\OpenClaw\workspace\process-guardian

echo Deleting old service...
sc delete GuardianDaemon >nul 2>&1

echo Creating new service...
sc create GuardianDaemon binPath= "D:\OpenClaw\workspace\process-guardian\service_wrapper.exe" start= auto DisplayName= "Guardian Daemon"

echo Starting service...
sc start GuardianDaemon

echo Done!
pause