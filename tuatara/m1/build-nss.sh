#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# Build NSPR and NSS from the tree with MSVC for i686: x87 only
# (/arch:IA32, which NSS's coreconf already sets for 32-bit x86), the static
# C runtime, optimised. Runs in MSYS2 (MINGW32) with an x86 MSVC environment.
#
#   build-nss.sh REPO_ROOT WORK_DIR [ntcompat|plain]
#
# NSS's make build expects nss/ and nspr/ side by side, so the sources are
# copied into WORK_DIR. Binaries end up in WORK_DIR/out.
#
# With "ntcompat" (the default), every DLL and program links ntcompat's
# objects, injected through MSVC's LINK environment variable (link.exe adds
# its contents to every link, so neither project's makefiles change):
# the import thunks for NT 3.51/4.0 (the "noyy" set too: no YY-Thunks here),
# the lazy-bound imports, the SIMD policy and the entropy fallback. NSS's
# freebl is compiled with TUATARA_NTCOMPAT so its CPU detection follows the
# SIMD policy. Afterwards the PE versions are set to 3.10.

set -euo pipefail
repo=$1
work=$2
mode=${3:-ntcompat}

rm -rf "$work"
mkdir -p "$work"
cp -r "$repo/nsprpub" "$work/nspr"
cp -r "$repo/security/nss" "$work/nss"

# NSS's and NSPR's make builds call nsinstall on Windows.
mkdir -p "$work/bin"
cp "$repo/tuatara/m1/nsinstall" "$work/bin/"
export PATH="$work/bin:$PATH"

make_extra=()
if [ "$mode" = ntcompat ]; then
    ntc="$repo/ntcompat"
    nobj="$work/ntcompat-obj"
    mkdir -p "$nobj"
    (
        cd "$nobj"
        for f in thunks/thunks.c thunks/lazy.c core/osversion.c cpu/simd_policy.c crypto/entropy.c; do
            cl -nologo -c -O2 -MT -arch:IA32 -W3 -Fo"$(basename "$f" .c).obj" "$(cygpath -w "$ntc/$f")"
        done
        for a in imp_x86 imp_noyy_x86 imp_lazy_x86; do
            ml -nologo -c -coff -safeseh -Fo"$a.obj" "$(cygpath -w "$ntc/thunks/$a.asm")"
        done
    )
    LINK=""
    for o in "$nobj"/*.obj; do LINK="$LINK \"$(cygpath -w "$o")\""; done
    export LINK="$LINK advapi32.lib"
    echo "LINK=$LINK"
    make_extra+=(XCFLAGS="-DTUATARA_NTCOMPAT -I$(cygpath -m "$ntc")")
fi

cd "$work/nss"
make_args=(
    OS_TARGET=WIN95          # NSPR's Windows 95-style build, as Mozilla uses
    BUILD_OPT=1              # optimised
    USE_STATIC_RTL=1         # /MT: no vcruntime140/ucrtbase DLLs
    NSS_DISABLE_GTESTS=1
    NSS_ENABLE_WERROR=0
)
run_make() {
    make "$@" "${make_args[@]}" "${make_extra[@]}" >> "$work/build.log" 2>&1 || {
        grep -nE "error|Error [0-9]|No such file|not found" "$work/build.log" | head -40
        tail -n 60 "$work/build.log"
        exit 1
    }
}
# nss_build_all configures and builds NSPR first, then NSS; its top level
# isn't safe to run in parallel. Only the libraries here: some tools (for
# example signtool) need a zlib this tree doesn't give NSS.
run_make nss_build_all DIRS="coreconf lib"
# Then just the tools the tests use.
run_make -C cmd export libs DIRS="lib tstclnt certutil selfserv"
tail -n 20 "$work/build.log"

dist=$(ls -d "$work"/dist/WIN95*_OPT.OBJ | head -1)
mkdir -p "$work/out"
cp "$dist"/lib/*.dll "$work/out/"
for tool in tstclnt certutil selfserv; do
    cp "$dist/bin/$tool.exe" "$work/out/" 2>/dev/null || true
done
if [ "$mode" = ntcompat ]; then
    # link.exe won't write PE versions below 5.01; NT 3.51 needs 3.10.
    python "$(cygpath -w "$repo/tuatara/tools/apiaudit")" set-version --os 3.10 --subsystem 3.10 \
        "$work"/out/*.dll "$work"/out/*.exe
fi
echo "NSPR/NSS build ${BUILD_ID:-local} ($mode)" > "$work/out/BUILD.TXT"
ls -la "$work/out"
