# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Minimal, dependency-free PE/COFF reader.

Reads only what apiaudit needs: headers, imports, delay imports, exports,
the TLS directory and the file version resource. Input files are treated
as untrusted: every offset is bounds-checked and every table has a size cap,
and malformed input raises PEError instead of crashing or looping.
"""

import struct
from dataclasses import dataclass, field
from typing import List, Optional

MACHINE_I386 = 0x014C
MACHINE_AMD64 = 0x8664

IMAGE_FILE_DLL = 0x2000

DIR_EXPORT = 0
DIR_IMPORT = 1
DIR_RESOURCE = 2
DIR_TLS = 9
DIR_DELAY_IMPORT = 13

RT_VERSION = 16

# Caps that no legitimate image comes near; they stop hostile files from
# making us loop for a long time.
MAX_SECTIONS = 96
MAX_DESCRIPTORS = 4096
MAX_THUNKS = 65536
MAX_EXPORTS = 65536
MAX_STRING = 4096


class PEError(Exception):
    """The file is not a PE image, or it is malformed."""


@dataclass
class Section:
    name: str
    virtual_address: int
    virtual_size: int
    raw_offset: int
    raw_size: int


@dataclass
class ImportedSymbol:
    name: Optional[str]      # None when imported by ordinal only
    ordinal: Optional[int]   # set when imported by ordinal
    hint: Optional[int] = None


@dataclass
class ImportedDll:
    dll: str
    symbols: List[ImportedSymbol]
    delay: bool = False
    # True when names could not be read: an old bound import that has no
    # import name table.
    unparsed: bool = False


@dataclass
class Export:
    ordinal: int
    name: Optional[str]
    forwarder: Optional[str]


@dataclass
class TlsInfo:
    raw_data_size: int
    callbacks: int


@dataclass
class PEImage:
    path: str
    machine: int
    characteristics: int
    is_pe32_plus: bool
    image_base: int
    os_version: tuple
    subsystem_version: tuple
    subsystem: int
    dll_characteristics: int
    sections: List[Section] = field(default_factory=list)
    imports: List[ImportedDll] = field(default_factory=list)
    exports: List[Export] = field(default_factory=list)
    export_name: Optional[str] = None
    tls: Optional[TlsInfo] = None
    file_version: Optional[str] = None

    @property
    def is_dll(self):
        return bool(self.characteristics & IMAGE_FILE_DLL)


class _Reader:
    def __init__(self, data: bytes, path: str):
        self.data = data
        self.path = path
        self.sections: List[Section] = []
        self.size_of_headers = 0

    def unpack(self, fmt, off):
        size = struct.calcsize(fmt)
        if off < 0 or off + size > len(self.data):
            raise PEError(f"read past end of file at 0x{off:x}")
        return struct.unpack_from(fmt, self.data, off)

    def rva_to_offset(self, rva):
        if rva < self.size_of_headers:
            return rva
        for s in self.sections:
            extent = max(s.virtual_size, s.raw_size)
            if s.virtual_address <= rva < s.virtual_address + extent:
                delta = rva - s.virtual_address
                if delta >= s.raw_size:
                    # The RVA is in the zero-filled tail of the section and
                    # has no bytes in the file.
                    raise PEError(f"RVA 0x{rva:x} is not backed by file data")
                return s.raw_offset + delta
        raise PEError(f"RVA 0x{rva:x} is outside every section")

    def cstring_at_rva(self, rva):
        off = self.rva_to_offset(rva)
        end = self.data.find(b"\0", off, off + MAX_STRING)
        if end < 0:
            raise PEError(f"unterminated string at RVA 0x{rva:x}")
        return self.data[off:end].decode("ascii", errors="replace")


def parse(path: str, data: Optional[bytes] = None) -> PEImage:
    """Parse the PE file at *path*, or *data* if given."""
    if data is None:
        with open(path, "rb") as f:
            data = f.read()
    r = _Reader(data, path)

    if data[:2] != b"MZ":
        raise PEError("no MZ signature")
    (e_lfanew,) = r.unpack("<I", 0x3C)
    if r.unpack("<4s", e_lfanew)[0] != b"PE\0\0":
        raise PEError("no PE signature")

    coff = e_lfanew + 4
    machine, nsections, _ts, _sym, _nsym, opt_size, characteristics = r.unpack(
        "<HHIIIHH", coff)
    if nsections > MAX_SECTIONS:
        raise PEError(f"implausible section count {nsections}")

    opt = coff + 20
    (magic,) = r.unpack("<H", opt)
    if magic == 0x10B:
        plus = False
        (image_base,) = r.unpack("<I", opt + 28)
        ndirs_off, dirs_off = opt + 92, opt + 96
    elif magic == 0x20B:
        plus = True
        (image_base,) = r.unpack("<Q", opt + 24)
        ndirs_off, dirs_off = opt + 108, opt + 112
    else:
        raise PEError(f"unknown optional header magic 0x{magic:x}")

    os_major, os_minor, _imaj, _imin, ss_major, ss_minor = r.unpack("<6H", opt + 40)
    (size_of_headers,) = r.unpack("<I", opt + 60)
    subsystem, dll_chars = r.unpack("<HH", opt + 68)
    (ndirs,) = r.unpack("<I", ndirs_off)
    ndirs = min(ndirs, 16)
    dirs = [r.unpack("<II", dirs_off + 8 * i) for i in range(ndirs)]
    dirs += [(0, 0)] * (16 - ndirs)

    r.size_of_headers = size_of_headers
    sec_off = opt + opt_size
    for i in range(nsections):
        name, vsize, va, rawsize, rawptr = r.unpack("<8sIIII", sec_off + 40 * i)
        r.sections.append(Section(name.rstrip(b"\0").decode("ascii", "replace"),
                                  va, vsize, rawptr, rawsize))

    img = PEImage(
        path=path, machine=machine, characteristics=characteristics,
        is_pe32_plus=plus, image_base=image_base,
        os_version=(os_major, os_minor),
        subsystem_version=(ss_major, ss_minor),
        subsystem=subsystem, dll_characteristics=dll_chars,
        sections=r.sections)

    if dirs[DIR_IMPORT][0]:
        img.imports += _parse_imports(r, dirs[DIR_IMPORT][0], plus)
    if dirs[DIR_DELAY_IMPORT][0]:
        img.imports += _parse_delay_imports(r, dirs[DIR_DELAY_IMPORT][0], plus, image_base)
    if dirs[DIR_EXPORT][0]:
        img.export_name, img.exports = _parse_exports(r, *dirs[DIR_EXPORT])
    if dirs[DIR_TLS][0]:
        img.tls = _parse_tls(r, dirs[DIR_TLS][0], plus, image_base)
    if dirs[DIR_RESOURCE][0]:
        try:
            img.file_version = _parse_file_version(r, dirs[DIR_RESOURCE][0])
        except PEError:
            img.file_version = None  # a broken version resource is not fatal
    return img


def pe_checksum(data: bytes, checksum_offset: int) -> int:
    """The PE image checksum (as computed by CheckSumMappedFile)."""
    total = 0
    n = len(data)
    padded = data + (b"\0" if n % 2 else b"")
    for (word,) in struct.iter_unpack("<H", padded):
        total += word
    # The checksum field itself counts as zero.
    lo, hi = struct.unpack_from("<HH", data, checksum_offset)
    total -= lo + hi
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (total + n) & 0xFFFFFFFF


def set_versions(path, os_version=None, subsystem_version=None):
    """Rewrite the OS and/or subsystem version fields of a PE file in place,
    and update its checksum. Returns the old (os, subsystem) versions."""
    with open(path, "rb") as f:
        data = bytearray(f.read())
    r = _Reader(bytes(data), path)
    if data[:2] != b"MZ":
        raise PEError("no MZ signature")
    (e_lfanew,) = r.unpack("<I", 0x3C)
    if r.unpack("<4s", e_lfanew)[0] != b"PE\0\0":
        raise PEError("no PE signature")
    opt = e_lfanew + 24
    (magic,) = r.unpack("<H", opt)
    if magic not in (0x10B, 0x20B):
        raise PEError(f"unknown optional header magic 0x{magic:x}")
    old_os = r.unpack("<HH", opt + 40)
    old_ss = r.unpack("<HH", opt + 48)
    if os_version is not None:
        struct.pack_into("<HH", data, opt + 40, *os_version)
    if subsystem_version is not None:
        struct.pack_into("<HH", data, opt + 48, *subsystem_version)
    struct.pack_into("<I", data, opt + 64, pe_checksum(bytes(data), opt + 64))
    with open(path, "wb") as f:
        f.write(data)
    return tuple(old_os), tuple(old_ss)


def _read_thunks(r, rva, plus):
    """Yield the thunk values in an import name table, up to the null entry."""
    fmt, step = ("<Q", 8) if plus else ("<I", 4)
    ordinal_flag = 1 << 63 if plus else 1 << 31
    off = r.rva_to_offset(rva)
    for _ in range(MAX_THUNKS):
        (v,) = r.unpack(fmt, off)
        if v == 0:
            return
        yield v, ordinal_flag
        off += step
    raise PEError("import thunk table is not terminated")


def _symbols_from_thunks(r, rva, plus, va_base=0):
    syms = []
    for v, flag in _read_thunks(r, rva, plus):
        if v & flag:
            syms.append(ImportedSymbol(name=None, ordinal=v & 0xFFFF))
        else:
            name_rva = (v & 0x7FFFFFFF) - va_base
            (hint,) = r.unpack("<H", r.rva_to_offset(name_rva))
            syms.append(ImportedSymbol(name=r.cstring_at_rva(name_rva + 2),
                                       ordinal=None, hint=hint))
    return syms


def _parse_imports(r, rva, plus):
    out = []
    off = r.rva_to_offset(rva)
    for _ in range(MAX_DESCRIPTORS):
        oft, tds, _fwd, name_rva, ft = r.unpack("<IIIII", off)
        off += 20
        if not (oft or name_rva or ft):
            return out
        dll = r.cstring_at_rva(name_rva)
        if oft:
            out.append(ImportedDll(dll, _symbols_from_thunks(r, oft, plus)))
        elif tds:
            # A bound import with no name table: the IAT holds addresses,
            # not names, so the imported names can't be recovered.
            out.append(ImportedDll(dll, [], unparsed=True))
        else:
            out.append(ImportedDll(dll, _symbols_from_thunks(r, ft, plus)))
    raise PEError("import directory is not terminated")


def _parse_delay_imports(r, rva, plus, image_base):
    out = []
    off = r.rva_to_offset(rva)
    for _ in range(MAX_DESCRIPTORS):
        attrs, name, _hmod, _iat, int_, _biat, _uiat, _ts = r.unpack("<8I", off)
        off += 32
        if not name:
            return out
        # Bit 0 of the attributes set means the fields are RVAs. VC6-era
        # images leave it clear and store virtual addresses instead.
        base = 0 if attrs & 1 else image_base
        dll = r.cstring_at_rva(name - base)
        syms = _symbols_from_thunks(r, int_ - base, plus, va_base=base) if int_ else []
        out.append(ImportedDll(dll, syms, delay=True))
    raise PEError("delay import directory is not terminated")


def _parse_exports(r, rva, size):
    off = r.rva_to_offset(rva)
    (_c, _ts, _maj, _min, name_rva, base, nfuncs, nnames,
     funcs_rva, names_rva, ords_rva) = r.unpack("<IIHHIIIIIII", off)
    if nfuncs > MAX_EXPORTS or nnames > MAX_EXPORTS:
        raise PEError("implausible export count")
    dll_name = r.cstring_at_rva(name_rva) if name_rva else None

    names_by_index = {}
    if nnames:
        noff = r.rva_to_offset(names_rva)
        ooff = r.rva_to_offset(ords_rva)
        for i in range(nnames):
            (nrva,) = r.unpack("<I", noff + 4 * i)
            (idx,) = r.unpack("<H", ooff + 2 * i)
            names_by_index.setdefault(idx, r.cstring_at_rva(nrva))

    exports = []
    if nfuncs:
        foff = r.rva_to_offset(funcs_rva)
        for i in range(nfuncs):
            (frva,) = r.unpack("<I", foff + 4 * i)
            if frva == 0:
                continue  # gap in the ordinal range
            fwd = r.cstring_at_rva(frva) if rva <= frva < rva + size else None
            exports.append(Export(ordinal=base + i, name=names_by_index.get(i),
                                  forwarder=fwd))
    return dll_name, exports


def _parse_tls(r, rva, plus, image_base):
    off = r.rva_to_offset(rva)
    if plus:
        start, end, _idx, cb_va = r.unpack("<QQQQ", off)
        fmt, step = "<Q", 8
    else:
        start, end, _idx, cb_va = r.unpack("<IIII", off)
        fmt, step = "<I", 4
    callbacks = 0
    if cb_va:
        try:
            coff = r.rva_to_offset(cb_va - image_base)
            while callbacks < 1024:
                (v,) = r.unpack(fmt, coff + step * callbacks)
                if not v:
                    break
                callbacks += 1
        except PEError:
            pass  # the callback array is only filled in at load time
    return TlsInfo(raw_data_size=max(0, end - start), callbacks=callbacks)


def _resource_first_data(r, root_off, type_id):
    """Return the RVA and size of the first resource of *type_id*."""
    def entries(dir_off):
        n_named, n_id = r.unpack("<HH", dir_off + 12)
        for i in range(min(n_named + n_id, 4096)):
            yield r.unpack("<II", dir_off + 16 + 8 * i)

    for name, target in entries(root_off):
        if name & 0x80000000 or name != type_id or not target & 0x80000000:
            continue
        node = root_off + (target & 0x7FFFFFFF)
        # Go down the name and language levels, taking the first entry.
        for _level in range(2):
            first = next(entries(node), None)
            if first is None:
                return None
            _n, target = first
            if not target & 0x80000000:
                break
            node = root_off + (target & 0x7FFFFFFF)
        else:
            return None
        data_rva, size, _cp, _res = r.unpack("<IIII", root_off + target)
        return data_rva, size
    return None


def _parse_file_version(r, rsrc_rva):
    found = _resource_first_data(r, r.rva_to_offset(rsrc_rva), RT_VERSION)
    if not found:
        return None
    data_rva, size = found
    off = r.rva_to_offset(data_rva)
    blob = r.data[off:off + min(size, 1 << 20)]
    sig = blob.find(b"\xbd\x04\xef\xfe")  # VS_FIXEDFILEINFO.dwSignature
    if sig < 0 or sig + 16 > len(blob):
        return None
    ms, ls = struct.unpack_from("<II", blob, sig + 8)
    return f"{ms >> 16}.{ms & 0xFFFF}.{ls >> 16}.{ls & 0xFFFF}"
