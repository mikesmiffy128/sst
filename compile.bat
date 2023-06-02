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
set dmodname= -DMODULE_NAME=%basename%
:: ugly annoying special cases
if "%dmodname%"==" -DMODULE_NAME=con_" set dmodname= -DMODULE_NAME=con
if "%dmodname%"==" -DMODULE_NAME=sst" set dmodname=
set objs=%objs% .build/%basename%.o
:: note: we use a couple of C23 things now because otherwise we'd have to wait a
:: year to get anything done. typeof=__typeof prevents pedantic warnings caused
:: by typeof still technically being an extension, and stdbool gives us
:: predefined bool/true/false before compilers start doing that by default
%CC% -m32 -c -flto %cflags% %warnings% -I.build/include -D_CRT_SECURE_NO_WARNINGS -D_DLL ^
-DWIN32_LEAN_AND_MEAN -DNOMINMAX%dmodname% -Dtypeof=__typeof -include stdbool.h ^
-o .build/%basename%.o %1 || exit /b
goto :eof

:src
goto :eof

:main

set src=
:: funny hack to build a list conveniently, lol.
setlocal EnableDelayedExpansion
for /f "tokens=2" %%f in ('findstr /B /C:":+ " "%~nx0"') do set src=!src! src/%%f
setlocal DisableDelayedExpansion
:+ ac.c
:+ bind.c
:+ crypto.c
:+ alias.c
:+ autojump.c
:+ con_.c
:+ democustom.c
:+ demorec.c
:+ engineapi.c
:+ ent.c
:+ errmsg.c
:+ extmalloc.c
:+ fixes.c
:+ fov.c
:+ gamedata.c
:+ gameinfo.c
:+ hook.c
:+ kv.c
:+ kvsys.c
:+ l4dmm.c
:+ l4dreset.c
:+ l4dwarp.c
:+ nomute.c
:+ nosleep.c
:+ portalcolours.c
:+ rinput.c
:+ sst.c
:+ x86.c
:: just tack these on, whatever (repeated condition because of expansion memes)
if "%dbg%"=="1" set src=%src% src/dbg.c
if "%dbg%"=="1" set src=%src% src/udis86.c

%HOSTCC% -municode -O2 %warnings% -D_CRT_SECURE_NO_WARNINGS -include stdbool.h -ladvapi32 ^
-o .build/codegen.exe src/build/codegen.c src/build/cmeta.c || exit /b
%HOSTCC% -municode -O2 %warnings% -D_CRT_SECURE_NO_WARNINGS -include stdbool.h ^
-o .build/mkgamedata.exe src/build/mkgamedata.c src/kv.c || exit /b
%HOSTCC% -municode -O2 %warnings% -D_CRT_SECURE_NO_WARNINGS -include stdbool.h -ladvapi32 ^
-o .build/mkentprops.exe src/build/mkentprops.c src/kv.c || exit /b
.build\codegen.exe%src% || exit /b
.build\mkgamedata.exe gamedata/engine.kv gamedata/gamelib.kv gamedata/inputsystem.kv gamedata/matchmaking.kv || exit /b
.build\mkentprops.exe gamedata/entprops.kv || exit /b
llvm-rc /FO .build\dll.res src\dll.rc || exit /b
%CC% -shared -O0 -w -o .build/tier0.dll src/stubs/tier0.c
%CC% -shared -O0 -w -o .build/vstdlib.dll src/stubs/vstdlib.c
for %%b in (%src%) do ( call :cc %%b || exit /b )
if "%dbg%"=="1" (
	:: ugh, microsoft.
	set clibs=-lmsvcrtd -lvcruntimed -lucrtd
) else (
	set clibs=-lmsvcrt -lvcruntime -lucrt
)
%CC% -shared -flto %ldflags% -Wl,/IMPLIB:.build/sst.lib,/Brepro,/nodefaultlib ^
-L.build %clibs% -lkernel32 -luser32 -ladvapi32 -lshlwapi -ld3d9 -ldsound ^
-ltier0 -lvstdlib -o sst.dll%objs% .build/dll.res || exit /b
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
