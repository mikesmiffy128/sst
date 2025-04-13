:: This file is dedicated to the public domain.
@echo off
setlocal

if not exist .build\ (
	md .build
	attrib +H .build
)
if not exist .build\include\ md .build\include

if "%CC%"=="" set CC=clang --target=i686-pc-windows-msvc
if "%HOSTCC%"=="" set HOSTCC=clang

set host64=0
(
	echo:#ifndef _WIN64
	echo:#error
	echo:#endif
) | %HOSTCC% -E - >nul 2>nul && set host64=1

set warnings=-Wall -pedantic -Wno-parentheses -Wno-missing-braces ^
-Wno-gnu-zero-variadic-macro-arguments -Werror=implicit-function-declaration ^
-Werror=vla

set dbg=0
:: XXX: -Og would be nice but apparently a bunch of stuff still gets inlined
:: which can be somewhat annoying so -O0 it is.
if "%dbg%"=="1" (
	set cflags=-O0 -g3 -fsanitize-trap=undefined
	set ldflags=-O0 -g3
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
%CC% -c -flto -mno-stack-arg-probe %cflags% %warnings% %stdflags% -I.build/include ^
-D_DLL%dmodname% -o .build/%basename%.o %1 || goto :end
goto :eof

:src
goto :eof

:main

set stdflags=-std=c2x -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX

set src=
:: funny hack to build a list conveniently, lol.
setlocal EnableDelayedExpansion
for /f "tokens=2" %%f in ('findstr /B /C:":+ " "%~nx0"') do set src=!src! src/%%f
setlocal DisableDelayedExpansion
:+ ac.c
:+ alias.c
:+ autojump.c
:+ bind.c
:+ clientcon.c
:+ con_.c
:+ chatrate.c
:+ chunklets/fastspin.c
:+ chunklets/msg.c
:+ crypto.c
:+ democustom.c
:+ demorec.c
:+ engineapi.c
:+ ent.c
:+ errmsg.c
:+ extmalloc.c
:+ fastfwd.c
:+ fixes.c
:+ fov.c
:+ gameinfo.c
:+ gameserver.c
:+ hexcolour.c
:+ hook.c
:+ hud.c
:+ inputhud.c
:+ kvsys.c
:+ l4d1democompat.c
:+ l4daddon.c
:+ l4dmm.c
:+ l4dreset.c
:+ l4dwarp.c
:+ nomute.c
:+ nosleep.c
:+ os.c
:+ portalcolours.c
:+ rinput.c
:+ sst.c
:+ trace.c
:+ x86.c
:+ xhair.c
:: just tack these on, whatever (repeated condition because of expansion memes)
if "%dbg%"=="1" set src=%src% src/dbg.c
if "%dbg%"=="1" set src=%src% src/udis86.c
if "%dbg%"=="0" set src=%src% src/wincrt.c

%CC% -fuse-ld=lld -shared -O0 -w -o .build/bcryptprimitives.dll -Wl,-def:src/stubs/bcryptprimitives.def src/stubs/bcryptprimitives.c
set lbcryptprimitives_host=-lbcryptprimitives
if %host64%==1 (
	:: note: no mangling madness on x64, so we can just call the linker directly
	lld-link -machine:x64 -def:src/stubs/bcryptprimitives.def -implib:.build/bcryptprimitives64.lib
	set lbcryptprimitives_host=-lbcryptprimitives64
)
%CC% -fuse-ld=lld -shared -O0 -w -o .build/tier0.dll src/stubs/tier0.c
%CC% -fuse-ld=lld -shared -O0 -w -o .build/vstdlib.dll src/stubs/vstdlib.c

%HOSTCC% -fuse-ld=lld -O2 %warnings% %stdflags% -include stdbool.h ^
-L.build %lbcryptprimitives_host% -o .build/gluegen.exe src/build/gluegen.c src/build/cmeta.c src/os.c || goto :end
%HOSTCC% -fuse-ld=lld -O2 %warnings% %stdflags% -include stdbool.h ^
-L.build %lbcryptprimitives_host% -o .build/mkgamedata.exe src/build/mkgamedata.c src/os.c || goto :end
%HOSTCC% -fuse-ld=lld -O2 %warnings% %stdflags% -include stdbool.h ^
-L.build %lbcryptprimitives_host% -o .build/mkentprops.exe src/build/mkentprops.c src/os.c || goto :end
.build\gluegen.exe%src% || goto :end
.build\mkgamedata.exe gamedata/engine.txt gamedata/gamelib.txt gamedata/inputsystem.txt ^
gamedata/matchmaking.txt gamedata/vgui2.txt gamedata/vguimatsurface.txt || goto :end
.build\mkentprops.exe gamedata/entprops.txt || goto :end
llvm-rc /FO .build\dll.res src\dll.rc || goto :end
for %%b in (%src%) do ( call :cc %%b || goto :end )
:: we need different library names for debugging because Microsoft...
:: actually, it's different anyway because we don't use vcruntime for releases
:: any more. see comment in wincrt.c
if "%dbg%"=="1" (
	set clibs=-lmsvcrtd -lvcruntimed -lucrtd
) else (
	set clibs=-lucrt
)
%CC% -fuse-ld=lld -shared -flto %ldflags% -Wl,/IMPLIB:.build/sst.lib,/Brepro,/nodefaultlib ^
-L.build %clibs% -lkernel32 -luser32 -lbcryptprimitives -lshlwapi -ld3d9 -ldsound ^
-ltier0 -lvstdlib -lntdll -o .build/sst.dll%objs% .build/dll.res || goto :end
:: get rid of another useless file (can we just not create this???)
del .build\sst.lib

:: awkward logic to replace sst.dll while it's potentially loaded, because
:: windows likes to lock things and/or doesn't have atomic rename.
:: very TOCTOU-ish code, not good at all, but sometimes bad is good enough
move /y .build\sst.dll sst.dll >nul 2>nul || (
	move /y sst.dll .build\sst.old.dll >nul 2>nul || (
		echo:ERROR: couldn't remove sst.dll OR .build/sst.old.dll - are both loaded?>&2
		goto :end
	)
	move .build\sst.dll sst.dll >nul 2>nul || (
		echo:ERROR: couldn't replace sst.dll - did something just try to load it?>&2
		goto :end
	)
)
:: try to cleanup the .old again - it'll fail if we just moved it but may work
:: in some eventual future invocation
if exist .build\sst.old.dll del .build\sst.old.dll >nul 2>nul

%HOSTCC% -fuse-ld=lld -O2 -g %warnings% %stdflags% -include test/test.h -o .build/bitbuf.test.exe test/bitbuf.test.c || goto :end
.build\bitbuf.test.exe || goto :end
:: special case: test must be 32-bit
%HOSTCC% -fuse-ld=lld -m32 -O2 -g %warnings% %stdflags% -L.build -lbcryptprimitives -include test/test.h -o .build/hook.test.exe test/hook.test.c || goto :end
.build\hook.test.exe || goto :end
%HOSTCC% -fuse-ld=lld -O2 -g %warnings% %stdflags% -include test/test.h -o .build/x86.test.exe test/x86.test.c || goto :end
.build\x86.test.exe || goto :end

:end
exit /b %errorlevel%

:: vi: sw=4 tw=4 noet tw=80 cc=80
