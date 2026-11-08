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
