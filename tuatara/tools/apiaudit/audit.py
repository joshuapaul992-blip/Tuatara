# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Check PE images against OS baselines and report what won't load."""

import fnmatch
import os
from collections import defaultdict
from dataclasses import asdict, dataclass
from typing import Dict, List, Optional

from . import pe
from .baseline import DllExports, OsBaseline

try:
    import tomllib
except ImportError:  # Python < 3.11
    tomllib = None

ERROR, WARNING = "error", "warning"

# What each check means, shown in the report legend and in --help.
CHECKS = {
    "parse-error": (ERROR, "file could not be parsed as a PE image"),
    "machine": (ERROR, "image is not i386"),
    "subsystem-version": (ERROR, "PE subsystem version is newer than the OS; the loader refuses it"),
    "os-version": (WARNING, "PE OS version field is newer than the OS"),
    "tls-directory": (ERROR, "DLL has implicit TLS; breaks when loaded with LoadLibrary before Vista"),
    "missing-dll": (ERROR, "imported DLL does not exist on this OS and is not shipped with us"),
    "missing-function": (ERROR, "imported function is not exported by the DLL on this OS"),
    "missing-ordinal": (ERROR, "imported ordinal is not exported by the DLL on this OS"),
    "ordinal-import": (WARNING, "import by ordinal from a system DLL; ordinals can differ between versions"),
    "forwarder-broken": (ERROR, "export is forwarded to a function that doesn't exist on this OS"),
    "forwarder-unknown": (WARNING, "export is forwarded to a DLL that has no baseline"),
    "bound-import": (WARNING, "bound import without a name table; names could not be checked"),
}


@dataclass
class Finding:
    os: str
    file: str
    check: str
    severity: str
    dll: Optional[str] = None
    function: Optional[str] = None
    kind: Optional[str] = None  # "static" or "delay" for import findings
    detail: Optional[str] = None
    allowed_by: Optional[int] = None  # index of the allowlist entry


@dataclass
class AllowEntry:
    index: int
    reason: str
    check: Optional[str] = None
    dll: Optional[str] = None
    function: Optional[str] = None
    file: Optional[str] = None
    os: Optional[List[str]] = None
    kind: Optional[str] = None
    fallback: Optional[str] = None
    uses: int = 0

    def matches(self, f: Finding):
        if self.check and self.check != f.check:
            return False
        if self.kind and self.kind != f.kind:
            return False
        if self.os and f.os not in self.os:
            return False
        if self.dll and not (f.dll and fnmatch.fnmatch(f.dll.lower(), self.dll.lower())):
            return False
        if self.function and not (f.function and fnmatch.fnmatchcase(f.function, self.function)):
            return False
        if self.file and not fnmatch.fnmatch(os.path.basename(f.file).lower(), self.file.lower()):
            return False
        return True


def load_allowlist(path) -> List[AllowEntry]:
    if tomllib is None:
        raise SystemExit("allowlists need Python 3.11 or newer (tomllib)")
    with open(path, "rb") as f:
        doc = tomllib.load(f)
    entries = []
    known = {"reason", "check", "dll", "function", "file", "os", "kind", "fallback"}
    for i, raw in enumerate(doc.get("allow", [])):
        unknown = set(raw) - known
        if unknown:
            raise SystemExit(f"{path}: allow entry {i + 1}: unknown keys {sorted(unknown)}")
        if not str(raw.get("reason", "")).strip():
            raise SystemExit(f"{path}: allow entry {i + 1} has no reason")
        if raw.get("check") and raw["check"] not in CHECKS:
            raise SystemExit(f"{path}: allow entry {i + 1}: unknown check {raw['check']!r}")
        if isinstance(raw.get("os"), str):
            raw["os"] = [raw["os"]]
        entries.append(AllowEntry(index=i + 1, **raw))
    return entries


def collect_files(paths):
    """Expand directories into the .exe and .dll files under them.

    Returns (path on disk, path to show in reports) pairs; files found in a
    directory are shown relative to it.
    """
    out = []
    for p in paths:
        if os.path.isdir(p):
            for root, dirs, files in os.walk(p):
                dirs.sort()
                for name in sorted(files):
                    if name.lower().endswith((".exe", ".dll")):
                        full = os.path.join(root, name)
                        out.append((full, os.path.relpath(full, p)))
        else:
            out.append((p, p))
    return out


def _local_exports(images) -> Dict[str, DllExports]:
    """Exports of the DLLs we ship, so imports between them resolve."""
    local = {}
    for img in images:
        if not img.is_dll:
            continue
        d = DllExports(dll=os.path.basename(img.path).lower(), file_version=img.file_version)
        for e in img.exports:
            d.by_ordinal[e.ordinal] = e.forwarder
            if e.name:
                d.by_name[e.name] = e.forwarder
        local[d.dll] = d
    return local


def _normalise_dll(name):
    name = name.lower()
    return name if "." in name else name + ".dll"


class Auditor:
    def __init__(self, baselines: Dict[str, OsBaseline], allowlist=None):
        self.baselines = baselines
        self.allowlist = allowlist or []

    def run(self, paths) -> List[Finding]:
        images, findings = [], []
        for path, shown in collect_files(paths):
            try:
                img = pe.parse(path)
            except (pe.PEError, OSError) as e:
                for os_id in self.baselines:
                    findings.append(self._finding(os_id, shown, "parse-error", detail=str(e)))
                continue
            img.path = shown
            images.append(img)
        local = _local_exports(images)
        for os_id, base in self.baselines.items():
            for img in images:
                findings += self._check_image(base, img, local)
        for f in findings:
            for entry in self.allowlist:
                if entry.matches(f):
                    f.allowed_by = entry.index
                    entry.uses += 1
                    break
        return findings

    def _finding(self, os_id, path, check, **kw):
        return Finding(os=os_id, file=path, check=check, severity=CHECKS[check][0], **kw)

    def _check_image(self, base: OsBaseline, img: pe.PEImage, local):
        out = []
        add = lambda check, **kw: out.append(self._finding(base.id, img.path, check, **kw))

        if img.machine != pe.MACHINE_I386:
            add("machine", detail=f"machine 0x{img.machine:04x}")
        if img.subsystem_version > base.version:
            add("subsystem-version", detail="%d.%02d > %d.%02d" % (img.subsystem_version + base.version))
        if img.os_version > base.version:
            add("os-version", detail="%d.%02d > %d.%02d" % (img.os_version + base.version))
        if img.is_dll and img.tls is not None:
            add("tls-directory", detail=f"{img.tls.raw_data_size} bytes of TLS data, "
                                        f"{img.tls.callbacks} callbacks")

        for imp in img.imports:
            kind = "delay" if imp.delay else "static"
            dll = _normalise_dll(imp.dll)
            system = base.find_dll(dll)
            exports = system or local.get(dll)
            if exports is None:
                add("missing-dll", dll=dll, kind=kind)
                continue
            if imp.unparsed:
                add("bound-import", dll=dll, kind=kind)
                continue
            for sym in imp.symbols:
                if sym.name is not None:
                    if sym.name not in exports.by_name:
                        add("missing-function", dll=dll, function=sym.name, kind=kind)
                        continue
                    fwd = exports.by_name[sym.name]
                    label = sym.name
                else:
                    label = f"#{sym.ordinal}"
                    if system is not None:
                        add("ordinal-import", dll=dll, function=label, kind=kind)
                    if sym.ordinal not in exports.by_ordinal:
                        add("missing-ordinal", dll=dll, function=label, kind=kind)
                        continue
                    fwd = exports.by_ordinal[sym.ordinal]
                if fwd and system is not None:
                    problem = self._resolve_forwarder(base, fwd)
                    if problem:
                        add(problem, dll=dll, function=label, kind=kind, detail=f"-> {fwd}")
        return out

    def _resolve_forwarder(self, base, fwd, depth=0):
        """Follow a forwarder chain; return a check name if it is broken."""
        if depth > 8 or "." not in fwd:
            return "forwarder-broken"
        target_dll, target = fwd.rsplit(".", 1)
        exports = base.find_dll(_normalise_dll(target_dll))
        if exports is None:
            return "forwarder-unknown"
        if target.startswith("#") and target[1:].isdigit():
            nxt = exports.by_ordinal.get(int(target[1:]), "missing")
        else:
            nxt = exports.by_name.get(target, "missing")
        if nxt == "missing":
            return "forwarder-broken"
        return self._resolve_forwarder(base, nxt, depth + 1) if nxt else None


def summarise(findings: List[Finding], baselines, allowlist, strict=False):
    """Return (report text, exit code)."""
    lines = []
    blocking = 0
    for os_id, base in baselines.items():
        mine = [f for f in findings if f.os == os_id]
        open_ = [f for f in mine if f.allowed_by is None]
        errors = sum(f.severity == ERROR for f in open_)
        warnings = sum(f.severity == WARNING for f in open_)
        allowed = len(mine) - len(open_)
        blocking += errors + (warnings if strict else 0)
        lines.append(f"== {os_id} ({base.name}): {errors} errors, {warnings} warnings, "
                     f"{allowed} allowed")

        # Image-level findings first, then imports grouped by DLL.
        for f in sorted((f for f in open_ if f.dll is None), key=lambda f: (f.file, f.check)):
            lines.append(f"  {f.severity:7} {f.check:18} {f.file}: {f.detail or ''}")
        by_dll = defaultdict(lambda: defaultdict(list))
        for f in open_:
            if f.dll is not None:
                key = (f.check, f.function or "", f.kind, f.detail or "")
                by_dll[f.dll][key].append(os.path.basename(f.file))
        for dll in sorted(by_dll):
            entries = by_dll[dll]
            lines.append(f"  {dll} ({len(entries)})")
            for (check, func, kind, detail), files in sorted(entries.items(),
                                                            key=lambda kv: (kv[0][1], kv[0][0])):
                tag = " [delay]" if kind == "delay" else ""
                extra = f" {detail}" if detail else ""
                used = ", ".join(sorted(set(files)))
                lines.append(f"    {func or '-':40} {check}{tag}{extra}  <- {used}")
        lines.append("")

    unused = [e for e in allowlist if e.uses == 0]
    for e in unused:
        lines.append(f"note: allowlist entry {e.index} matched nothing (stale?): {e.reason}")
    return "\n".join(lines).rstrip() + "\n", (1 if blocking else 0)


def findings_as_json(findings):
    return [asdict(f) for f in findings]
