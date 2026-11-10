#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# Build SpiderMonkey's standalone shell (js.exe) from the tree with MSVC for
# i686: x87 only, the static C runtime (TUATARA_STATIC_RTL), ntcompat linked
# in, PE versions set to 3.10. Runs in MozillaBuild's MSYS with an x86 MSVC
# environment and MozillaBuild's Python 2.7 on PATH.
#
#   build-js.sh REPO_ROOT WORK_DIR
#
# First pass without jemalloc. ICU (Intl) is always built in this tree. The
# JIT stays in; whether it may run is decided at runtime.

set -euo pipefail
repo=$1
work=$2
mkdir -p "$work/out"
work=$(cd "$work" && pwd)

# The runner's PATH holds other toolchains that configure would pick up
# (Strawberry Perl's pkg-config script, MinGW's gcc, Git's Unix tools).
clean_path=
IFS=: read -r -a _entries <<< "$PATH"
for _e in "${_entries[@]}"; do
    case "$(echo "$_e" | tr 'A-Z' 'a-z')" in
        *strawberry*|*mingw64*|*/git/usr/bin*|*/git/mingw64*|*cygwin*|*/msys64/*) ;;
        *) clean_path="${clean_path:+$clean_path:}$_e" ;;
    esac
done
export PATH="$clean_path"

. "$repo/tuatara/m1/ntcompat-env.sh" "$repo" "$work/ntcompat-obj"
export TUATARA_STATIC_RTL=1
# Sanity check: every ntcompat object readable, and a trivial DLL links
# with them (DLL links failed with LNK1107 before any input was processed).
(
    cd "$work/ntcompat-obj"
    for o in *.obj; do
        dumpbin -nologo -headers "$o" > /dev/null 2>&1 && echo "ok  $o" || echo "BAD $o"
    done
    printf 'int __stdcall DllMain(void *a, unsigned b, void *c) { return 1; }\n' > dlltest.c
    cl -nologo -LD -MT dlltest.c -Fedlltest-with.dll > dlltest-with.log 2>&1 &&
        echo "trivial DLL links with ntcompat" || { echo "trivial DLL fails with ntcompat:"; cat dlltest-with.log; }
    LINK= cl -nologo -LD -MT dlltest.c -Fedlltest-without.dll > dlltest-without.log 2>&1 &&
        echo "trivial DLL links without ntcompat" || { echo "trivial DLL fails without ntcompat:"; cat dlltest-without.log; }
    rm -f dlltest*.dll dlltest*.lib dlltest*.exp dlltest.obj
)

# The JIT's CPU detection follows the SIMD policy (TUATARA_NTCOMPAT).
export CFLAGS="-DTUATARA_NTCOMPAT -I$NTCOMPAT_DIR"
# Nothing in the tree sets the C++ standard; MSVC defaults to C++14 and the
# in-tree ICU needs C++17.
export CXXFLAGS="$CFLAGS -std:c++17"

# configure and old-configure are generated from configure.in and
# old-configure.in by autoconf 2.13.
(cd "$repo/js/src" && autoconf-2.13 && autoconf-2.13 old-configure.in > old-configure)

mkdir -p "$work/js-obj"
cd "$work/js-obj"
"$repo/js/src/configure" \
    --target=i686-pc-mingw32 --host=i686-pc-mingw32 \
    --enable-optimize --disable-debug \
    --disable-jemalloc --disable-shared-js \
    > "$work/configure.log" 2>&1 || {
        tail -n 80 "$work/configure.log"
        tail -n 80 config.log 2>/dev/null || true
        exit 1
    }
tail -n 15 "$work/configure.log"

mozmake -j"$(nproc 2>/dev/null || echo 4)" > "$work/build.log" 2>&1 || {
    grep -nE " error |error C[0-9]+|LNK[0-9]+|fatal error|\*\*\*" "$work/build.log" | head -40
    tail -n 60 "$work/build.log"
    if grep -q "LNK1107" "$work/build.log"; then
        # Which file does link.exe choke on? Re-run one failing link verbosely.
        # The mozglue link fails even without ntcompat, and its inputs all pass
        # dumpbin. link.exe names *itself* as the corrupt file. Run the same
        # link by hand, adding the tree's options group by group.
        echo "=== link.exe: $(type -p link)"
        echo "=== LIB as native programs see it:"; cmd //c "echo %LIB%" 2>/dev/null || true
        (
            cd mozglue/build
            objs="../../memory/mozalloc/winheap.obj ../../memory/mozalloc/Unified_cpp_memory_mozalloc0.obj
                  ../misc/StackWalk.obj ../misc/TimeStamp.obj ../misc/TimeStamp_windows.obj
                  ../../mfbt/Compression.obj ../../mfbt/Decimal.obj ../../mfbt/Unified_cpp_mfbt0.obj"
            libs="kernel32.lib user32.lib gdi32.lib winmm.lib wsock32.lib advapi32.lib psapi.lib dbghelp.lib delayimp.lib"
            try() {
                echo "=== link $*"
                LINK= link -NOLOGO -DLL -OUT:t.dll -SUBSYSTEM:WINDOWS,5.01 -MACHINE:X86 "$@" $objs $libs 2>&1 |
                    grep -vE "^ +Creating|^$" | tail -6
                rm -f t.dll t.lib t.exp t.pdb t.ilk
            }
            try
            try -DEF:mozglue.def
            try -DEF:mozglue.def -DEBUG -DEBUGTYPE:CV -PDB:t.pdb
            try -DEF:mozglue.def -LARGEADDRESSAWARE -NXCOMPAT -DYNAMICBASE -SAFESEH
            try -DEF:mozglue.def -DELAYLOAD:dbghelp.dll -DELAYLOAD:user32.dll -DELAYLOAD:version.dll
            try -DEF:mozglue.def -OPT:REF
            try -DEF:mozglue.def -VERBOSE
        ) > "$work/link-verbose.log" 2>&1
        grep -vE "^ +(Searching|Found|Referenced|Loaded|Processed)" "$work/link-verbose.log" | head -60
    fi
    exit 1
}
tail -n 10 "$work/build.log"

cp dist/bin/js.exe "$work/out/"
cp dist/bin/*.dll "$work/out/" 2>/dev/null || true
python "$(winpath "$repo/tuatara/tools/apiaudit")" set-version --os 3.10 --subsystem 3.10 \
    "$work"/out/*.exe $(ls "$work"/out/*.dll 2>/dev/null || true)
echo "SpiderMonkey shell build ${BUILD_ID:-local}" > "$work/out/BUILD.TXT"
ls -la "$work/out"
