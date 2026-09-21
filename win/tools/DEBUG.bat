@echo off
cd /d "%~dp0..\app"
python -m pip install pynput pywin32 psutil
python agent.py
pause

