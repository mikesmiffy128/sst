:: This file is dedicated to the public domain.
@echo off

:: In several old L4D2 builds, we currently have some weird black magic we don't
:: fully understand to do what looks like DRM circumvention or... something.
:: Annoyingly, that black magic manages to break regular use of Steam after the
:: game exits. This is fixed by setting a registry key back to Steam's PID.

:: The scripts used to launch those builds already do this, of course, but if
:: you're launching L4D2 under a debugger, you can use this script instead.

:: By the way, if anyone wants to look into solving the root cause so that none
:: of this is needed any more, that would be cool!

set REG=%SYSTEMROOT%\SysWOW64\reg.exe
if not exist "%REG%" set REG=%SYSTEMROOT%\System32\reg.exe
set steampid=
for /F "usebackq skip=1 delims=" %%I in (
	`wmic process where "name='steam.exe'" get processid 2^>nul`
) do ( set steampid=%%I & goto :ok)
:ok
if not %steampid%=="" (
	%REG% add "HKCU\SOFTWARE\Valve\Steam\ActiveProcess" /f /t REG_DWORD ^
/v pid /d %steampid%>nul
)

:: vi: sw=4 ts=4 noet tw=80 cc=80
