@echo off
REM TOTALLY_WIPE — stops everything and removes all traces from THIS pc.
REM Run as the logged-in user. Double-click, done.
setlocal
echo [*] killing agent processes...
taskkill /F /IM pythonw.exe /FI "WINDOWTITLE eq" >nul 2>&1
wmic process where "name='pythonw.exe' and commandline like '%%cachemgr.py%%'" delete >nul 2>&1
wmic process where "name='pythonw.exe' and commandline like '%%SysCache%%'" delete >nul 2>&1
wmic process where "name='python.exe' and commandline like '%%cachemgr.py%%'" delete >nul 2>&1
taskkill /F /FI "WINDOWTITLE eq SysCache*" >nul 2>&1

echo [*] removing startup hooks...
reg delete HKCU\Software\Microsoft\Windows\CurrentVersion\Run /v SysCache /f >nul 2>&1
del /f /q "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\SysCache.lnk" >nul 2>&1
schtasks /delete /f /tn "SysCache" >nul 2>&1

echo [*] deleting payload + logs...
rmdir /s /q "%LOCALAPPDATA%\.cache\SysCache" >nul 2>&1
rmdir /s /q "%LOCALAPPDATA%\.cache\.sysdata" >nul 2>&1
rmdir /s /q "%LOCALAPPDATA%\.cache" >nul 2>&1
del /f /q "%TEMP%\syscache.lock" >nul 2>&1

echo [*] verifying...
tasklist /FI "IMAGENAME eq pythonw.exe" 2>nul | find /i "pythonw" >nul && echo [!] pythonw still running (check Task Manager) || echo [OK] no pythonw running
reg query HKCU\Software\Microsoft\Windows\CurrentVersion\Run /v SysCache >nul 2>&1 && echo [!] Run key still present || echo [OK] Run key gone
if exist "%LOCALAPPDATA%\.cache\SysCache" (echo [!] payload dir still present) else echo [OK] payload gone
echo done.
pause
