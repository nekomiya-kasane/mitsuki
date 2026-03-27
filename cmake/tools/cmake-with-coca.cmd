@echo off
REM Forward to PowerShell wrapper so CMake Tools runs setup.py before every configure/build.
setlocal
set "HERE=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%cmake-with-coca.ps1" %*
