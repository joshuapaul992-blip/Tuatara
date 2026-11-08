# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""API baselines: the exports of each system DLL on one target OS.

Layout on disk:

    baselines/<os-id>/os.json        {"id", "name", "version": [maj, min], "build"}
    baselines/<os-id>/<dll>.json     {"dll", "file_version", "machine", "exports": [...]}

Each export is {"ordinal": int, "name": str|null, "forwarder": str|null}.
Only export names and ordinals are stored, never Microsoft binaries.
"""

import json
import os
from dataclasses import dataclass, field
from typing import Dict, Optional

from . import pe

FORMAT_VERSION = 1


@dataclass
class DllExports:
    dll: str
    file_version: Optional[str]
    by_name: Dict[str, Optional[str]] = field(default_factory=dict)     # name -> forwarder
    by_ordinal: Dict[int, Optional[str]] = field(default_factory=dict)  # ordinal -> forwarder


@dataclass
class OsBaseline:
    id: str
    name: str
    version: tuple
    build: Optional[int]
    dlls: Dict[str, DllExports] = field(default_factory=dict)  # lower-case dll name

    def find_dll(self, dll):
        return self.dlls.get(dll.lower())


def _dll_record(img: pe.PEImage, filename: str):
    exports = sorted(
        ({"ordinal": e.ordinal, "name": e.name, "forwarder": e.forwarder}
         for e in img.exports),
        key=lambda e: e["ordinal"])
    return {
        "format": FORMAT_VERSION,
        "dll": filename.lower(),
        "file_version": img.file_version,
        "machine": f"0x{img.machine:04x}",
        "exports": exports,
    }


def write_baseline(dll_dir, out_dir, os_id, os_name, version, build=None, log=print):
    """Build a baseline from a directory of (already expanded) system DLLs."""
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "os.json"), "w") as f:
        json.dump({"format": FORMAT_VERSION, "id": os_id, "name": os_name,
                   "version": list(version), "build": build}, f, indent=2)
        f.write("\n")
    count = 0
    for entry in sorted(os.listdir(dll_dir)):
        path = os.path.join(dll_dir, entry)
        if not os.path.isfile(path) or not entry.lower().endswith((".dll", ".drv", ".exe")):
            continue
        try:
            img = pe.parse(path)
        except pe.PEError as e:
            log(f"skipping {entry}: {e}")
            continue
        if not img.exports:
            continue
        with open(os.path.join(out_dir, entry.lower() + ".json"), "w") as f:
            json.dump(_dll_record(img, entry), f, indent=1, sort_keys=True)
            f.write("\n")
        count += 1
    return count


DUMP_MAGIC = "TUATARA-NTEXPORTS"

# Ids for the systems Tuatara targets, by (major, minor, service pack text).
KNOWN_SYSTEMS = {
    (3, 51, "Service Pack 5"): ("nt351-sp5", "Windows NT 3.51 SP5"),
    (4, 0, "Service Pack 6"): ("nt4-sp6a", "Windows NT 4.0 SP6a"),
    (5, 0, "Service Pack 4"): ("w2k-sp4", "Windows 2000 SP4"),
}


def parse_dump(path):
    """Read a file written by ntexports.exe.

    Returns (os info dict, {dll: record}, [missing dll names]).
    """
    os_info, dlls, missing, current = None, {}, [], None
    with open(path, encoding="latin-1") as f:
        lines = f.read().splitlines()
    if not lines or not lines[0].startswith(DUMP_MAGIC + "\t"):
        raise ValueError(f"{path}: not an ntexports dump")
    ended = False
    for n, line in enumerate(lines[1:], start=2):
        if not line:
            continue
        cols = line.split("\t")
        kind = cols[0]
        try:
            if kind == "os":
                os_info = {"major": int(cols[1]), "minor": int(cols[2]),
                           "build": int(cols[3]), "sp": cols[4] if len(cols) > 4 else ""}
            elif kind == "dll":
                name = cols[1].lower()
                current = {"format": FORMAT_VERSION, "dll": name,
                           "file_version": None if cols[2] == "-" else cols[2],
                           "machine": f"0x{int(cols[3], 16):04x}", "exports": []}
                dlls[name] = current
            elif kind == "e":
                if current is None:
                    raise ValueError("export before any dll line")
                current["exports"].append({"ordinal": int(cols[1]),
                                           "name": cols[2] or None,
                                           "forwarder": (cols[3] if len(cols) > 3 else "") or None})
            elif kind == "missing":
                missing.append(cols[1].lower())
            elif kind == "end":
                ended = True
                break
            else:
                raise ValueError(f"unknown record {kind!r}")
        except (IndexError, ValueError) as e:
            raise ValueError(f"{path}:{n}: {e}") from None
    if not ended:
        raise ValueError(f"{path}: truncated (no 'end' line); copy the whole file")
    if os_info is None:
        raise ValueError(f"{path}: no 'os' line")
    return os_info, dlls, missing


def guess_system(os_info):
    """(id, name) for a dump's OS, or None if it isn't a known target."""
    sp = os_info["sp"].strip()
    if os_info["major"] == 4 and sp.startswith("Service Pack 6"):
        sp = "Service Pack 6"  # SP6a reports itself as "Service Pack 6"
    return KNOWN_SYSTEMS.get((os_info["major"], os_info["minor"], sp))


def write_baseline_from_dump(dump_path, out_root, os_id=None, os_name=None, log=print):
    os_info, dlls, missing = parse_dump(dump_path)
    guessed = guess_system(os_info)
    if os_id is None:
        if guessed is None:
            raise ValueError(
                f"{dump_path}: Windows {os_info['major']}.{os_info['minor']} "
                f"{os_info['sp']!r} is not a known target; pass --os-id and --name")
        os_id, os_name = guessed
    os_name = os_name or (guessed[1] if guessed else os_id)
    out_dir = os.path.join(out_root, os_id)
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "os.json"), "w") as f:
        json.dump({"format": FORMAT_VERSION, "id": os_id, "name": os_name,
                   "version": [os_info["major"], os_info["minor"]],
                   "build": os_info["build"], "service_pack": os_info["sp"],
                   "source": "ntexports", "missing_dlls": sorted(missing)}, f, indent=2)
        f.write("\n")
    for name, rec in sorted(dlls.items()):
        rec["exports"].sort(key=lambda e: e["ordinal"])
        with open(os.path.join(out_dir, name + ".json"), "w") as f:
            json.dump(rec, f, indent=1, sort_keys=True)
            f.write("\n")
    log(f"{os_id}: {len(dlls)} DLLs, {sum(len(r['exports']) for r in dlls.values())} exports; "
        f"not present: {', '.join(missing) or 'none'}")
    return os_id


def load_os(path) -> OsBaseline:
    with open(os.path.join(path, "os.json")) as f:
        meta = json.load(f)
    base = OsBaseline(id=meta["id"], name=meta["name"],
                      version=tuple(meta["version"]), build=meta.get("build"))
    for entry in sorted(os.listdir(path)):
        if not entry.endswith(".json") or entry == "os.json":
            continue
        with open(os.path.join(path, entry)) as f:
            rec = json.load(f)
        d = DllExports(dll=rec["dll"], file_version=rec.get("file_version"))
        for e in rec["exports"]:
            d.by_ordinal[e["ordinal"]] = e["forwarder"]
            if e["name"]:
                d.by_name[e["name"]] = e["forwarder"]
        base.dlls[d.dll] = d
    return base


def load_all(root, only=None):
    """Load every OS baseline under *root*, or only the ids in *only*."""
    out = {}
    for entry in sorted(os.listdir(root)):
        path = os.path.join(root, entry)
        if os.path.isfile(os.path.join(path, "os.json")):
            b = load_os(path)
            if only is None or b.id in only:
                out[b.id] = b
    if only:
        missing = set(only) - set(out)
        if missing:
            raise SystemExit(f"no baseline for: {', '.join(sorted(missing))}")
    return out
