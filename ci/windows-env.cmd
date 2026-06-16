@echo off
call "%~1" %~2 >nul
if errorlevel 1 exit /b %errorlevel%
set
