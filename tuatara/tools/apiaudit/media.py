# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Collect system DLLs from extracted Windows install media and service packs.

Install CDs store most files compressed, with the last letter of the
extension replaced by an underscore (KERNEL32.DL_):

  * SZDD ("MS-DOS COMPRESS", LZSS) is expanded here, with no dependencies.
  * MSCF (cabinet) is expanded with the external `cabextract` tool.

Pass the source directories in order (base media first, then each service
pack): a file from a later directory replaces the same file from an earlier one.
"""

import os
import shutil
import subprocess
import tempfile

SZDD_MAGIC = b"SZDD\x88\xf0\x27\x33"
CAB_MAGIC = b"MSCF"

# The DLLs apiaudit baselines. Lower-case base names.
DEFAULT_DLLS = [
    "advapi32", "comctl32", "comdlg32", "crypt32", "gdi32", "imm32", "kernel32",
    "msimg32", "msvcrt", "mswsock", "ntdll", "ole32", "oleacc", "oleaut32",
    "psapi", "rpcrt4", "secur32", "shell32", "shfolder", "shlwapi", "user32",
    "usp10", "uxtheme", "version", "winmm", "winspool", "wininet", "ws2_32",
    "wsock32",
]


class MediaError(Exception):
    pass


def expand_szdd(data: bytes) -> bytes:
    """Expand an SZDD-compressed file (the format of MS-DOS COMPRESS.EXE)."""
    if not data.startswith(SZDD_MAGIC) or len(data) < 14:
        raise MediaError("not an SZDD file")
    if data[8:9] != b"A":
        raise MediaError(f"unknown SZDD mode {data[8:9]!r}")
    expected = int.from_bytes(data[10:14], "little")
    window = bytearray(b" " * 4096)
    pos = 4096 - 16
    out = bytearray()
    i = 14
    n = len(data)
    while i < n and len(out) < expected:
        control = data[i]
        i += 1
        for bit in range(8):
            if i >= n or len(out) >= expected:
                break
            if control & (1 << bit):
                b = data[i]
                i += 1
                out.append(b)
                window[pos] = b
                pos = (pos + 1) & 0xFFF
            else:
                if i + 1 >= n:
                    raise MediaError("truncated SZDD back-reference")
                lo, hi = data[i], data[i + 1]
                i += 2
                src = lo | ((hi & 0xF0) << 4)
                for k in range((hi & 0x0F) + 3):
                    b = window[(src + k) & 0xFFF]
                    out.append(b)
                    window[pos] = b
                    pos = (pos + 1) & 0xFFF
    if len(out) != expected:
        raise MediaError(f"SZDD size mismatch: got {len(out)}, expected {expected}")
    return bytes(out)


def _expand_cab(path, workdir):
    if shutil.which("cabextract") is None:
        raise MediaError(f"{path} is a cabinet; install cabextract to expand it")
    dest = tempfile.mkdtemp(dir=workdir)
    subprocess.run(["cabextract", "-q", "-d", dest, path], check=True,
                   stdout=subprocess.DEVNULL)
    files = [os.path.join(r, f) for r, _d, fs in os.walk(dest) for f in fs]
    if len(files) != 1:
        raise MediaError(f"{path}: expected one file in the cabinet, found {len(files)}")
    with open(files[0], "rb") as f:
        return f.read()


def _candidates(src_dir, wanted):
    """Map each wanted DLL base name to its file in *src_dir*, searched recursively."""
    found = {}
    for root, dirs, files in os.walk(src_dir):
        dirs.sort()
        for name in sorted(files):
            low = name.lower()
            base, ext = os.path.splitext(low)
            if base in wanted and ext in (".dll", ".dl_"):
                # Prefer an expanded .dll over a compressed .dl_ in the same tree.
                if base not in found or ext == ".dll":
                    found[base] = os.path.join(root, name)
    return found


def collect(src_dirs, out_dir, dlls=None, log=print):
    """Expand the wanted DLLs from *src_dirs* (in override order) into *out_dir*."""
    wanted = set(d.lower() for d in (dlls or DEFAULT_DLLS))
    os.makedirs(out_dir, exist_ok=True)
    chosen = {}
    for src in src_dirs:
        for base, path in _candidates(src, wanted).items():
            chosen[base] = path  # later sources override earlier ones
    with tempfile.TemporaryDirectory() as work:
        for base, path in sorted(chosen.items()):
            with open(path, "rb") as f:
                data = f.read()
            if data.startswith(SZDD_MAGIC):
                data = expand_szdd(data)
            elif data.startswith(CAB_MAGIC):
                data = _expand_cab(path, work)
            elif not data.startswith(b"MZ"):
                raise MediaError(f"{path}: unknown format")
            with open(os.path.join(out_dir, base + ".dll"), "wb") as f:
                f.write(data)
            log(f"{base}.dll  <- {path}")
    missing = sorted(wanted - set(chosen))
    if missing:
        log("not on this media: " + ", ".join(missing))
    return sorted(chosen), missing
