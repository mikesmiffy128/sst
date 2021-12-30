:: This file is dedicated to the public domain.
@echo off

if not exist .build\ (
	md .build
	attrib +H .build
)
if not exist .build\include\ md .build\include

set warnings=-Wall -pedantic -Wno-parentheses -Wno-missing-braces

set objs=
goto :main

:cc
for /F %%b in ("%1") do set basename=%%~nb
set objs=%objs% .build/%basename%.o
clang -m32 -c -O2 -flto %warnings% -I.build/include -D_CRT_SECURE_NO_WARNINGS ^
-DFILE_BASENAME=%basename% -o .build/%basename%.o %1 || exit /b
goto :eof

:main
clang -municode -O2 -fuse-ld=lld %warnings% -D_CRT_SECURE_NO_WARNINGS -ladvapi32 ^
-o .build/codegen.exe src/build/codegen.c src/build/cmeta.c || exit /b
clang -municode -O2 -fuse-ld=lld %warnings% -D_CRT_SECURE_NO_WARNINGS -ladvapi32 ^
-o .build/mkgamedata.exe src/build/mkgamedata.c src/kv.c || exit /b
.build\codegen.exe src/autojump.c src/con_.c src/demorec.c src/dbg.c src/fixes.c ^
src/gamedata.c src/gameinfo.c src/hook.c src/kv.c src/sst.c src/udis86.c || exit /b
.build\mkgamedata.exe gamedata/engine.kv gamedata/gamelib.kv || exit /b
:: llvm-rc doesn't preprocess, looks like it might later:
:: https://reviews.llvm.org/D100755?id=339141
:: in the meantime, manually run through clang -E
clang -E -xc src/dll.rc>.build\dll.pp.rc || exit /b
llvm-rc /FO .build\dll.res .build\dll.pp.rc || exit /b
:: might as well remove the temp file afterwards
del .build\dll.pp.rc
clang -m32 -shared -fuse-ld=lld -O0 -w -o .build/tier0.dll src/tier0stub.c
call :cc src/autojump.c || exit /b
call :cc src/con_.c || exit /b
call :cc src/demorec.c || exit /b
call :cc src/dbg.c || exit /b
call :cc src/extmalloc.c || exit /b
call :cc src/fixes.c || exit /b
call :cc src/gamedata.c || exit /b
call :cc src/gameinfo.c || exit /b
call :cc src/hook.c || exit /b
call :cc src/kv.c || exit /b
call :cc src/sst.c || exit /b
call :cc src/udis86.c || exit /b
clang -m32 -shared -O2 -flto -fuse-ld=lld -Wl,/implib:.build/sst.lib,/Brepro ^
-L.build -ladvapi32 -ltier0 -lshlwapi -o sst.dll%objs% .build/dll.res || exit /b
:: get rid of another useless file (can we just not create this???)
del .build\sst.lib

clang -fuse-ld=lld -O2 -g3 -include test/test.h -o .build/bitbuf.test.exe test/bitbuf.test.c || exit /b
.build\bitbuf.test.exe || exit /b
:: special case: test must be 32-bit
clang -m32 -fuse-ld=lld -O2 -g3 -ladvapi32 -include test/test.h -o .build/hook.test.exe test/hook.test.c || exit /b
.build\hook.test.exe || exit /b
clang -fuse-ld=lld -O2 -g3 -include test/test.h -o .build/kv.test.exe test/kv.test.c || exit /b
.build\kv.test.exe || exit /b

:: vi: sw=4 tw=4 noet tw=80 cc=80
