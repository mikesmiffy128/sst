:: This file is dedicated to the public domain.
@echo off
setlocal

if not exist .build\ (
	md .build
	attrib +H .build
)
if not exist .build\include\ md .build\include

:: XXX: still need to publicly document all the toolchain stuff
if not defined SST_TOOLCHAINS_ROOT goto :notoolchain
set _clang="%SST_TOOLCHAINS_ROOT%\llvm\bin\clang" -Xmicrosoft-windows-sys-root "%SST_TOOLCHAINS_ROOT%\msvc"
set CC=%_clang% --target=i686-pc-windows-msvc
:: note: we just assume x64 host here because toolchains repo currently assumes
:: assumes that too. supporting 32-bit hosts with that would be complex and
:: almost certainly completely useless lol
set HOSTCC=%_clang% --target=x86_64-pc-windows-msvc
set host64=1
:: the llvm distro I've got currently doesn't have an lld-link.exe but does have
:: an ld.lld.exe; this may change in the future I suppose so support both.
if exist "%SST_TOOLCHAINS_ROOT%\llvm\bin\lld-link.exe" (
	set LLDLINK="%SST_TOOLCHAINS_ROOT%\llvm\bin\lld-link"
) else (
	set LLDLINK="%SST_TOOLCHAINS_ROOT%\llvm\bin\ld.lld.exe" -flavor link
)
set LLVMRC="%SST_TOOLCHAINS_ROOT%\llvm\bin\llvm-rc.exe"
goto :toolchaindone

:notoolchain
if not defined CC set CC=clang --target=i686-pc-windows-msvc
if not defined HOSTCC set HOSTCC=clang
:: XXX: kinda non-standard variables here, not sure what else would make sense?
if not defined LLDLINK set LLDLINK=lld-link
if not defined LLVMRC set LLVMRC=llvm-rc
(
	echo:#ifndef _WIN64
	echo:#error
	echo:#endif
) | %HOSTCC% -E - >nul 2>nul && set host64=1
:toolchaindone

set warnings=-Wall -pedantic -Wno-parentheses -Wno-missing-braces ^
-Wno-gnu-zero-variadic-macro-arguments -Werror=implicit-function-declaration ^
-Werror=vla

set dbg=0
if "%~1"=="dbg" set dbg=1
set onething=%~2

:: XXX: -Og would be nice but apparently a bunch of stuff still gets inlined
:: which can be somewhat annoying so -O0 it is.
if "%dbg%"=="1" (
	set cflags=-O0 -g3 -masm=intel -fsanitize-trap=undefined -DSST_DBG
	set ldflags=-O0 -g3
) else (
	set cflags=-flto -O2 -masm=intel -ffreestanding
	set ldflags=-flto -O2 -ffreestanding
)

set objs=
goto :main

:cc
for /F %%f in ("%1") do (
	set basename=%%~nf
	set ext=%%~xf
)
set dmodname= -DMODULE_NAME=%basename%
:: ugly annoying special cases
if "%dmodname%"==" -DMODULE_NAME=con_" set dmodname= -DMODULE_NAME=con
if "%dmodname%"==" -DMODULE_NAME=sst" set dmodname=
set objs=%objs% .build/%basename%%ext%.o
:: extremely poor man's incremental build: manually specify one changed source
:: file by basename. the full source list is still put together so everything
:: gets relinked from existing objects. would obviously be good to have a real
:: build system, but this can be occasionally handy and took very low effort :)
if not "%onething%"=="" if not "src/%onething%.c"=="%1" exit /b 0
%CC% -c -mno-stack-arg-probe %cflags% %warnings% %stdflags% -include src/1.h ^
-I.build/include -D_DLL%dmodname% -o .build/%basename%%ext%.o %1
exit /b %errorlevel%

:main

set stdflags=-std=c2x -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN -DNOMINMAX

set src=
:: metasrc has C sources only -> we don't attempt to run gluegen over asm files.
:: we could also leave out chunklets, but that doesn't really matter that much.
set metasrc=
:: funny hack to build a list conveniently, lol.
setlocal EnableDelayedExpansion
for /f "tokens=2" %%f in ('findstr /B /C:":+ " "%~nx0"') do (
	set src=!src! src/%%f
	if "%%~xf"==".c" set metasrc=!metasrc! src/%%f
)
setlocal DisableDelayedExpansion
:+ ac.c
:+ alias.c
:+ autojump.c
:+ bind.c
:+ clientcon.c
:+ con_.c
:+ con_.S
:+ chatrate.c
:+ chunklets/fastspin.c
:+ chunklets/msg.c
:+ chunklets/x86.c
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
:+ portalisg.c
:+ rinput.c
:+ speedrun.c
:+ sst.c
:+ trace.c
:+ xhair.c
:: just tack these on, whatever (repeated condition because of expansion memes)
if "%dbg%"=="1" (
	set src=%src% src/dbg.c
	set metasrc=%metasrc% src/dbg.c
)
if "%dbg%"=="1" set src=%src% src/udis86.c
if "%dbg%"=="0" set src=%src% src/wincrt.c

if defined onething goto :skiphost
%CC% -fuse-ld=lld -shared -O0 -w -o .build/bcryptprimitives.dll -Wl,-def:src/stubs/bcryptprimitives.def src/stubs/bcryptprimitives.c
set lbcryptprimitives_host=-lbcryptprimitives
if %host64%==1 (
	:: note: no mangling madness on x64, so we can just call the linker directly
	%LLDLINK% -machine:x64 -def:src/stubs/bcryptprimitives.def -implib:.build/bcryptprimitives64.lib
	set lbcryptprimitives_host=-lbcryptprimitives64
)
%CC% -fuse-ld=lld -shared -O0 -w -o .build/tier0.dll src/stubs/tier0.c
%CC% -fuse-ld=lld -shared -O0 -w -o .build/vstdlib.dll src/stubs/vstdlib.c

%HOSTCC% -fuse-ld=lld -O2 %warnings% %stdflags% -include stdbool.h ^
-L.build %lbcryptprimitives_host% -o .build/gluegen.exe src/build/gluegen.c src/build/cmeta.c src/os.c src/chunklets/clex.c || goto :end
%HOSTCC% -fuse-ld=lld -O2 %warnings% %stdflags% -include stdbool.h ^
-L.build %lbcryptprimitives_host% -o .build/mkgamedata.exe src/build/mkgamedata.c src/os.c || goto :end
%HOSTCC% -fuse-ld=lld -O2 %warnings% %stdflags% -include stdbool.h ^
-L.build %lbcryptprimitives_host% -o .build/mkentprops.exe src/build/mkentprops.c src/os.c || goto :end
.build\gluegen.exe%metasrc% || goto :end
.build\mkgamedata.exe gamedata/engine.txt gamedata/gamelib.txt gamedata/inputsystem.txt ^
gamedata/matchmaking.txt gamedata/vgui2.txt gamedata/vguimatsurface.txt gamedata/vphysics.txt || goto :end
.build\mkentprops.exe gamedata/entprops.txt || goto :end
%LLVMRC% /FO .build\dll.res src\dll.rc || goto :end

:skiphost
for %%b in (%src%) do ( call :cc %%b || goto :end )
:: we need different library names for debugging because Microsoft...
:: actually, it's different anyway because we don't use vcruntime for releases
:: any more. see comment in wincrt.c
if "%dbg%"=="1" (
	set clibs=-lmsvcrtd -lvcruntimed -lucrtd
) else (
	set clibs=-lucrt
)
%CC% -fuse-ld=lld -shared %ldflags% -Wl,-implib:.build/sst.lib,-Brepro,-nodefaultlib,-safeseh:no ^
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

:: can also skip recompiling the tests on partial recompiles, probably
:: (technically not always safe to do, but whatever, fine in practice)
if not defined onething (
	%HOSTCC% -fuse-ld=lld -O2 -g %warnings% %stdflags% -include test/test.h -o .build/bitbuf.test.exe test/bitbuf.test.c || goto :end
	%HOSTCC% -fuse-ld=lld -O2 -g %warnings% %stdflags% -include test/test.h -o .build/x86.test.exe test/x86.test.c || goto :end
)
.build\bitbuf.test.exe || goto :end
.build\x86.test.exe || goto :end

:end
exit /b %errorlevel%

:: vi: sw=4 tw=4 noet tw=80 cc=80
