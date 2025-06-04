:: This file is dedicated to the public domain.
@echo off
setlocal

if not "%WINDBG_BIN%"=="" goto :ok
set WINDBG_BIN=tools\windbg\bin
if exist tools\windbg\bin\DbgX.Shell.exe goto :ok
powershell tools\windbg\install.ps1 || goto :end

:ok
%WINDBG_BIN%\DbgX.Shell.exe /g /c $^<tools\windbg\initcmds

:end
exit /b %errorlevel%

:: vi: sw=4 ts=4 noet tw=80 cc=80
