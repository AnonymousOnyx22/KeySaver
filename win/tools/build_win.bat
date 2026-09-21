@echo off
setlocal
REM auto-load MSVC env if cl not on PATH
where cl >nul 2>&1
if errorlevel 1 (
  for /f "delims=" %%V in ('dir /b "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" 2^>nul') do set VS="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
  if not defined VS for /f "delims=" %%V in ('dir /b "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" 2^>nul') do set VS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
  if defined VS call %VS% -arch=x64
)
where cl >nul 2>&1
if errorlevel 1 echo ERROR: run this from VS Developer Prompt ^(or install VS Build Tools^) & pause & exit /b 1
cd /d "%~dp0"
cl /nologo /O2 /DUNICODE /D_UNICODE /MT ..\src\agent_win.cpp ..\src\uploader_win.cpp /link winhttp.lib shell32.lib ole32.lib advapi32.lib /SUBSYSTEM:WINDOWS /OUT:..\app\.kbd.exe
if errorlevel 1 echo BUILD FAILED & pause & exit /b 1
echo built .kbd.exe OK
pause
