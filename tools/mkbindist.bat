:: This file is dedicated to the public domain.
@echo off

:: make a windows binary release - will eventually need a linux one too, but
:: worry about that later.
:: NOTE: requires 7-zip, either in the default installation dir or %SEVENZIP%

call compile.bat || exit /B
if not exist release\ md release
if "%SEVENZIP%"=="" set SEVENZIP=C:\Program Files\7-Zip\7z.exe
setlocal EnableDelayedExpansion
for /F "tokens=* usebackq" %%x IN (`^(echo VERSION_MAJOR ^& echo VERSION_MINOR^) ^| ^
		clang -x c -E -include src\version.h - ^| findstr /v #`) do (
	:: dumb but works:
	if "!major!"=="" set major=%%x
	set minor=%%x
)
setlocal DisableDelayedExpansion
set name=sst-v%major%.%minor%-BETA-win32
md TEMP-%name% || exit /B
copy sst.dll TEMP-%name%\sst.dll || exit /B
copy dist\LICENCE.windows TEMP-%name%\LICENCE || exit /B
:: using midnight on release day to make zip deterministic! change on next release!
powershell (Get-Item TEMP-%name%\sst.dll).LastWriteTime = new-object DateTime 2024, 5, 28, 0, 0, 0
powershell (Get-Item TEMP-%name%\LICENCE).LastWriteTime = new-object DateTime 2024, 5, 28, 0, 0, 0
pushd TEMP-%name%
"%SEVENZIP%" a -mtc=off %name%.zip sst.dll LICENCE || exit /B
move %name%.zip ..\release\%name%.zip
popd
rd /s /q TEMP-%name%\ || exit /B
