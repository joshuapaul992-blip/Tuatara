@echo off
rem This Source Code Form is subject to the terms of the Mozilla Public
rem License, v. 2.0. If a copy of the MPL was not distributed with this
rem file, You can obtain one at http://mozilla.org/MPL/2.0/.
rem
rem Build the M0 toolchain spike with one candidate toolchain.
rem
rem   build.cmd msvc    OUTDIR   candidate A: cl + link, then apiaudit set-version
rem   build.cmd clangcl OUTDIR   candidate B: clang-cl + lld-link /subsystem:...,3.10
rem   build.cmd noyy    OUTDIR   variant C: candidate A without YY-Thunks (ntcompat only),
rem                              to tell YY-Thunks problems from others on old systems
rem
rem Run from an x86 Visual Studio developer prompt (vcvarsall x86).
rem YYTHUNKS_OBJ must point at YY_Thunks_for_Win2K.obj. For candidate B,
rem YYTHUNKS_LIB may point at YY-Thunks' import-library directory for Windows
rem 2000 (lld-link can't use the .obj form; the directory holds replacement
rem kernel32.lib and friends, searched before the SDK's). Without it, B links
rem without YY-Thunks.

setlocal enableextensions
set CANDIDATE=%~1
set OUT=%~2
if "%CANDIDATE%"=="" goto usage
if "%OUT%"=="" goto usage
if /i not "%CANDIDATE%"=="noyy" if not exist "%YYTHUNKS_OBJ%" (
    echo build.cmd: YYTHUNKS_OBJ does not point at YY_Thunks_for_Win2K.obj
    exit /b 2
)
set SPIKE=%~dp0
set ROOT=%~dp0..\..
if not exist "%OUT%" mkdir "%OUT%"

rem x87 only (/arch:IA32); no thread-safe statics, which use implicit TLS.
set COMMON=/nologo /O2 /MT /W3 /arch:IA32 /Zc:threadSafeInit- /D_CRT_SECURE_NO_WARNINGS
set CXXONLY=/EHsc /std:c++17
set LIBS=user32.lib gdi32.lib advapi32.lib kernel32.lib

if /i "%CANDIDATE%"=="msvc" goto msvc
if /i "%CANDIDATE%"=="noyy" goto noyy
if /i "%CANDIDATE%"=="clangcl" goto clangcl
goto usage

:msvc
set CC=cl
set TAG=A-msvc
set SETVERSION=1
set YY=%YYTHUNKS_OBJ%
set LINKEXE=link /nologo /subsystem:console
set LINKDLL=link /nologo /dll /entry:DllMainCRTStartupForYY_Thunks
set LINKRAW=link /nologo /subsystem:console /nodefaultlib /entry:entry
goto build

:noyy
set CC=cl
set TAG=C-msvc-noyy
set SETVERSION=1
set YY=
set LINKEXE=link /nologo /subsystem:console
set LINKDLL=link /nologo /dll
set LINKRAW=link /nologo /subsystem:console /nodefaultlib /entry:entry
goto build

:clangcl
rem -march=i686: the Pentium Pro baseline (cmov, cmpxchg8b); clang-cl defaults lower.
set CC=clang-cl -m32 -march=i686
set TAG=B-clang-cl
set SETVERSION=
if defined YYTHUNKS_LIB (
    set YY=/libpath:"%YYTHUNKS_LIB%"
) else (
    echo build.cmd: YYTHUNKS_LIB not set; linking candidate B without YY-Thunks
    set YY=
)
set LINKEXE=lld-link /nologo /machine:x86 /subsystem:console,3.10
set LINKDLL=lld-link /nologo /machine:x86 /dll /subsystem:windows,3.10
if defined YYTHUNKS_LIB set LINKDLL=%LINKDLL% /entry:DllMainCRTStartupForYY_Thunks
set LINKRAW=lld-link /nologo /machine:x86 /subsystem:console,3.10 /nodefaultlib /entry:entry
goto build

:build
echo === %TAG%: compiling
rem ntcompat: the SIMD policy and the NT 3.51 / NT 4.0 import thunks.
rem /safeseh marks the assembled object SafeSEH-compatible (it has no handlers);
rem without it lld-link refuses to link and link.exe drops the SafeSEH table.
%CC% %COMMON% /c "%ROOT%\ntcompat\cpu\simd_policy.c" /Fo"%OUT%\simd_policy.obj" || exit /b 1
%CC% %COMMON% /c "%ROOT%\ntcompat\core\osversion.c" /Fo"%OUT%\osversion.obj" || exit /b 1
%CC% %COMMON% /c "%ROOT%\ntcompat\thunks\thunks.c" /Fo"%OUT%\thunks.obj" || exit /b 1
ml /nologo /c /coff /safeseh /Fo"%OUT%\imp_x86.obj" "%ROOT%\ntcompat\thunks\imp_x86.asm" || exit /b 1
set NTCOMPAT="%OUT%\imp_x86.obj" "%OUT%\thunks.obj" "%OUT%\osversion.obj" "%OUT%\simd_policy.obj"
%CC% %COMMON% %CXXONLY% /DHELLO_NT_TOOLCHAIN=\"%TAG%\" /c "%SPIKE%hello-nt\hello.cpp" /Fo"%OUT%\hello.obj" || exit /b 1
%CC% %COMMON% %CXXONLY% /c "%SPIKE%hello-nt\hellodll.cpp" /Fo"%OUT%\hellodll.obj" || exit /b 1
rem No C runtime at all: no security cookie, no stack probes.
%CC% /nologo /O1 /GS- /W3 /arch:IA32 /c "%SPIKE%ntexports\ntexports.c" /Fo"%OUT%\ntexports.obj" || exit /b 1

echo === %TAG%: linking
rem ntcompat's objects come first so its __imp_ symbols win over the import libraries.
rem Map files turn a CRASH line's module offset into a function name.
%LINKEXE% /out:"%OUT%\HELLO-NT.EXE" /map:"%OUT%\HELLO-NT.MAP" "%OUT%\hello.obj" %NTCOMPAT% %YY% %LIBS% || exit /b 1
%LINKDLL% /out:"%OUT%\HELLODLL.DLL" /map:"%OUT%\HELLODLL.MAP" "%OUT%\hellodll.obj" %NTCOMPAT% %YY% advapi32.lib kernel32.lib || exit /b 1
%LINKRAW% /out:"%OUT%\NTEXPORT.EXE" "%OUT%\ntexports.obj" kernel32.lib || exit /b 1

if defined SETVERSION (
    echo === %TAG%: setting PE versions to 3.10 ^(link.exe won't write them^)
    python "%ROOT%\tuatara\tools\apiaudit" set-version --os 3.10 --subsystem 3.10 ^
        "%OUT%\HELLO-NT.EXE" "%OUT%\HELLODLL.DLL" "%OUT%\NTEXPORT.EXE" || exit /b 1
)
del "%OUT%\*.obj" "%OUT%\*.lib" "%OUT%\*.exp" 2>nul
echo === %TAG%: done
exit /b 0

:usage
echo usage: build.cmd msvc^|noyy^|clangcl OUTDIR
exit /b 2
