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
# First pass: no jemalloc and no ICU (Intl), to keep the build small. The JIT
# stays in; whether it may run is decided at runtime.

set -euo pipefail
repo=$1
work=$2
mkdir -p "$work/out"
work=$(cd "$work" && pwd)

. "$repo/tuatara/m1/ntcompat-env.sh" "$repo" "$work/ntcompat-obj"
export TUATARA_STATIC_RTL=1

# old-configure is generated from old-configure.in by autoconf 2.13.
(cd "$repo/js/src" && autoconf-2.13 old-configure.in > old-configure)

mkdir -p "$work/js-obj"
cd "$work/js-obj"
"$repo/js/src/configure" \
    --target=i686-pc-mingw32 --host=i686-pc-mingw32 \
    --enable-optimize --disable-debug \
    --disable-jemalloc --without-intl-api \
    > "$work/configure.log" 2>&1 || {
        tail -n 80 "$work/configure.log"
        tail -n 80 config.log 2>/dev/null || true
        exit 1
    }
tail -n 15 "$work/configure.log"

mozmake -j"$(nproc 2>/dev/null || echo 4)" > "$work/build.log" 2>&1 || {
    grep -nE " error |error C[0-9]+|LNK[0-9]+|fatal error|\*\*\*" "$work/build.log" | head -40
    tail -n 60 "$work/build.log"
    exit 1
}
tail -n 10 "$work/build.log"

cp dist/bin/js.exe "$work/out/"
cp dist/bin/*.dll "$work/out/" 2>/dev/null || true
python "$(winpath "$repo/tuatara/tools/apiaudit")" set-version --os 3.10 --subsystem 3.10 \
    "$work"/out/*.exe $(ls "$work"/out/*.dll 2>/dev/null || true)
echo "SpiderMonkey shell build ${BUILD_ID:-local}" > "$work/out/BUILD.TXT"
ls -la "$work/out"
