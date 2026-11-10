#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# Source this from a build script running in MSYS (MSYS2 or MozillaBuild's)
# with an x86 MSVC environment:
#
#   . ntcompat-env.sh REPO_ROOT OBJ_DIR
#
# It compiles ntcompat (the import thunks for NT 3.51/4.0, including the
# "noyy" and "lazy" sets, the SIMD policy and the entropy fallback) into
# OBJ_DIR and exports LINK so link.exe adds those objects to every link,
# whatever build system drives it. It also defines:
#
#   winpath PATH        print PATH as a Windows path (D:\...)
#   winpath_m PATH      the same with forward slashes (D:/...)
#   NTCOMPAT_DIR        REPO_ROOT/ntcompat as a Windows path, for -I

winpath() {
    local d b
    d=$(cd "$(dirname "$1")" && pwd -W) || return 1
    b=$(basename "$1")
    echo "${d}/${b}" | sed 's|/|\\|g'
}
winpath_m() {
    local d
    d=$(cd "$(dirname "$1")" && pwd -W) || return 1
    echo "${d}/$(basename "$1")"
}

_ntc_repo=$1
_ntc_obj=$2
mkdir -p "$_ntc_obj"
(
    set -e
    cd "$_ntc_obj"
    for f in thunks/thunks.c thunks/lazy.c core/osversion.c cpu/simd_policy.c crypto/entropy.c; do
        cl -nologo -c -O2 -MT -arch:IA32 -W3 -Fo"$(basename "$f" .c).obj" "$(winpath "$_ntc_repo/ntcompat/$f")"
    done
    for a in imp_x86 imp_noyy_x86 imp_lazy_x86; do
        ml -nologo -c -coff -safeseh -Fo"$a.obj" "$(winpath "$_ntc_repo/ntcompat/thunks/$a.asm")"
    done
) || { echo "ntcompat-env.sh: building ntcompat failed" >&2; return 1; }

# Forward slashes: MozillaBuild's older MSYS rewrites environment values
# that look like paths (d:\... has a colon) when it starts native programs.
LINK=""
for _o in "$_ntc_obj"/*.obj; do LINK="$LINK $(winpath_m "$_o")"; done
export LINK="$LINK advapi32.lib"
NTCOMPAT_DIR=$(winpath_m "$_ntc_repo/ntcompat")
echo "ntcompat: LINK=$LINK"
echo "ntcompat: as native programs see it:"
cmd //c "echo %LINK%" 2>/dev/null || true
