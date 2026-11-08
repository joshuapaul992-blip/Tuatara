#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# Build NSPR and NSS from the tree with MSVC for i686: x87 only
# (/arch:IA32, which NSS's coreconf already sets for 32-bit x86), the static
# C runtime, optimised. Runs in MSYS2 (MINGW32) with an x86 MSVC environment.
#
#   build-nss.sh REPO_ROOT WORK_DIR
#
# NSS's make build expects nss/ and nspr/ side by side, so the sources are
# copied into WORK_DIR. Binaries end up in WORK_DIR/out.

set -euo pipefail
repo=$1
work=$2

rm -rf "$work"
mkdir -p "$work"
cp -r "$repo/nsprpub" "$work/nspr"
cp -r "$repo/security/nss" "$work/nss"

# NSS's and NSPR's make builds call nsinstall on Windows.
mkdir -p "$work/bin"
cp "$repo/tuatara/m1/nsinstall" "$work/bin/"
export PATH="$work/bin:$PATH"

cd "$work/nss"
make_args=(
    OS_TARGET=WIN95          # NSPR's Windows 95-style build, as Mozilla uses
    BUILD_OPT=1              # optimised
    USE_STATIC_RTL=1         # /MT: no vcruntime140/ucrtbase DLLs
    NSS_DISABLE_GTESTS=1
    NSS_ENABLE_WERROR=0
)
# nss_build_all configures and builds NSPR first, then NSS. Its top level
# isn't safe to run in parallel.
make nss_build_all "${make_args[@]}" > "$work/build.log" 2>&1 || {
    grep -nE "error|Error [0-9]|No such file|not found" "$work/build.log" | head -40
    tail -n 60 "$work/build.log"
    exit 1
}
tail -n 20 "$work/build.log"

dist=$(ls -d "$work"/dist/WIN95*_OPT.OBJ | head -1)
mkdir -p "$work/out"
cp "$dist"/lib/*.dll "$work/out/"
for tool in tstclnt certutil selfserv; do
    cp "$dist/bin/$tool.exe" "$work/out/" 2>/dev/null || true
done
echo "NSPR/NSS build $BUILD_ID" > "$work/out/BUILD.TXT"
ls -la "$work/out"
