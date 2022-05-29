:: This file is dedicated to the public domain.
@echo off

:: don't leak vars into the environment
setlocal

if not exist .build\ (
	md .build
	attrib +H .build
)
if not exist .build\include\ md .build\include

if "%CC%"=="" set CC=clang --target=i686-pc-windows-msvc -fuse-ld=lld
if "%HOSTCC%"=="" set HOSTCC=clang -fuse-ld=lld

set warnings=-Wall -pedantic -Wno-parentheses -Wno-missing-braces ^
-Wno-gnu-zero-variadic-macro-arguments

set dbg=0
if "%dbg%"=="1" (
	set cflags=-Og -g
	set ldflags=-Og -g
) else (
	set cflags=-O2
	set ldflags=-O2
)

set objs=
goto :main

:cc
for /F %%b in ("%1") do set basename=%%~nb
set objs=%objs% .build/%basename%.o
%CC% -m32 -c -flto %cflags% %warnings% -I.build/include -D_CRT_SECURE_NO_WARNINGS -D_DLL ^
-DWIN32_LEAN_AND_MEAN -DNOMINMAX -DFILE_BASENAME=%basename% -o .build/%basename%.o %1 || exit /b
goto :eof

:main
%HOSTCC% -municode -O2 %warnings% -D_CRT_SECURE_NO_WARNINGS ^
-o .build/codegen.exe src/build/codegen.c src/build/cmeta.c || exit /b
%HOSTCC% -municode -O2 %warnings% -D_CRT_SECURE_NO_WARNINGS ^
-o .build/mkgamedata.exe src/build/mkgamedata.c src/kv.c || exit /b
%HOSTCC% -municode -O2 %warnings% -D_CRT_SECURE_NO_WARNINGS -ladvapi32 ^
-o .build/mkentprops.exe src/build/mkentprops.c src/kv.c || exit /b
.build\codegen.exe src/autojump.c src/con_.c src/demorec.c src/engineapi.c src/ent.c src/extmalloc.c src/fixes.c src/fov.c ^
src/gamedata.c src/gameinfo.c src/hook.c src/kv.c src/l4dwarp.c src/nosleep.c src/portalcolours.c src/rinput.c src/sst.c src/x86.c || exit /b
.build\mkgamedata.exe gamedata/engine.kv gamedata/gamelib.kv gamedata/inputsystem.kv || exit /b
.build\mkentprops.exe gamedata/entprops.kv || exit /b
llvm-rc /FO .build\dll.res src\dll.rc || exit /b
%CC% -shared -O0 -w -o .build/tier0.dll src/stubs/tier0.c
%CC% -shared -O0 -w -o .build/vstdlib.dll src/stubs/vstdlib.c
call :cc src/autojump.c || exit /b
call :cc src/con_.c || exit /b
call :cc src/demorec.c || exit /b
call :cc src/engineapi.c || exit /b
call :cc src/ent.c || exit /b
call :cc src/extmalloc.c || exit /b
call :cc src/fixes.c || exit /b
call :cc src/fov.c || exit /b
call :cc src/gamedata.c || exit /b
call :cc src/gameinfo.c || exit /b
call :cc src/hook.c || exit /b
call :cc src/kv.c || exit /b
call :cc src/l4dwarp.c || exit /b
call :cc src/nosleep.c || exit /b
call :cc src/portalcolours.c || exit /b
call :cc src/rinput.c || exit /b
call :cc src/sst.c || exit /b
call :cc src/x86.c || exit /b
if "%dbg%"=="1" (
	call :cc src/dbg.c || exit /b
	call :cc src/udis86.c || exit /b
)
if "%dbg%"=="1" (
	:: ugh, microsoft.
	set clibs=-lmsvcrtd -lvcruntimed -lucrtd
) else (
	set clibs=-lmsvcrt -lvcruntime -lucrt
)
%CC% -shared -flto %ldflags% -Wl,/IMPLIB:.build/sst.lib,/Brepro,/nodefaultlib:libcmt ^
-L.build %clibs% -luser32 -ladvapi32 -lshlwapi -ld3d9 -ltier0 -lvstdlib -o sst.dll%objs% .build/dll.res || exit /b
:: get rid of another useless file (can we just not create this???)
del .build\sst.lib

%HOSTCC% -O2 -g -include test/test.h -o .build/bitbuf.test.exe test/bitbuf.test.c || exit /b
.build\bitbuf.test.exe || exit /b
:: special case: test must be 32-bit
%HOSTCC% -m32 -O2 -g -ladvapi32 -include test/test.h -o .build/hook.test.exe test/hook.test.c || exit /b
.build\hook.test.exe || exit /b
%HOSTCC% -O2 -g -include test/test.h -o .build/kv.test.exe test/kv.test.c || exit /b
.build\kv.test.exe || exit /b

endlocal

:: vi: sw=4 tw=4 noet tw=80 cc=80
