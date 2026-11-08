# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import argparse
import json
import os
import sys

from . import audit, baseline, media, pe


def _version(text):
    try:
        major, minor = text.split(".")
        return int(major), int(minor)
    except ValueError:
        raise argparse.ArgumentTypeError(f"expected MAJOR.MINOR, got {text!r}")


def cmd_check(args):
    only = args.os.split(",") if args.os else None
    bases = baseline.load_all(args.baselines, only)
    if not bases:
        raise SystemExit(f"no baselines found under {args.baselines}")
    allow = audit.load_allowlist(args.allowlist) if args.allowlist else []
    findings = audit.Auditor(bases, allow).run(args.paths)
    text, code = audit.summarise(findings, bases, allow, strict=args.strict)
    sys.stdout.write(text)
    if args.json:
        with open(args.json, "w") as f:
            json.dump(audit.findings_as_json(findings), f, indent=1)
            f.write("\n")
    return code


def cmd_media(args):
    dlls = args.dlls.split(",") if args.dlls else None
    media.collect(args.sources, args.out, dlls)
    return 0


def cmd_baseline(args):
    if args.from_dump:
        try:
            baseline.write_baseline_from_dump(args.from_dump, args.out, args.os_id, args.name)
        except ValueError as e:
            print(f"apiaudit: {e}", file=sys.stderr)
            return 2
        return 0
    if not (args.dll_dir and args.os_id and args.name and args.version):
        print("apiaudit: give DLL_DIR with --os-id, --name and --version, "
              "or --from-dump FILE", file=sys.stderr)
        return 2
    out = os.path.join(args.out, args.os_id)
    n = baseline.write_baseline(args.dll_dir, out, args.os_id, args.name,
                                args.version, args.build)
    print(f"wrote {n} DLL baselines to {out}")
    return 0


def cmd_set_version(args):
    for path in args.files:
        old_os, old_ss = pe.set_versions(path, args.os, args.subsystem)
        print(f"{path}: os %d.%02d -> %s, subsystem %d.%02d -> %s" % (
            old_os + (_fmt(args.os),) + old_ss + (_fmt(args.subsystem),)))
    return 0


def _fmt(v):
    return "unchanged" if v is None else "%d.%02d" % v


def cmd_dump(args):
    img = pe.parse(args.file)
    print(f"{img.path}: {'DLL' if img.is_dll else 'EXE'} machine=0x{img.machine:04x} "
          f"subsystem={img.subsystem} subsystem-version=%d.%02d os-version=%d.%02d"
          % (img.subsystem_version + img.os_version))
    if img.file_version:
        print(f"file version {img.file_version}")
    if img.tls:
        print(f"TLS: {img.tls.raw_data_size} bytes, {img.tls.callbacks} callbacks")
    for imp in img.imports:
        tag = " (delay)" if imp.delay else ""
        print(f"import {imp.dll}{tag}")
        for s in imp.symbols:
            print(f"    {s.name if s.name is not None else '#%d' % s.ordinal}")
    if img.exports:
        print(f"exports ({img.export_name}):")
        for e in img.exports:
            fwd = f" -> {e.forwarder}" if e.forwarder else ""
            print(f"    @{e.ordinal} {e.name or '(no name)'}{fwd}")
    return 0


def build_parser():
    legend = "\n".join(f"  {name:18} {sev:7} {text}"
                       for name, (sev, text) in audit.CHECKS.items())
    p = argparse.ArgumentParser(
        prog="apiaudit",
        description="Check Windows PE images against API baselines of old Windows NT versions.",
        epilog="checks:\n" + legend,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)

    c = sub.add_parser("check", help="audit .exe/.dll files against baselines")
    c.add_argument("paths", nargs="+", help="files or directories (searched recursively)")
    c.add_argument("--baselines", required=True, help="directory holding <os-id>/ baselines")
    c.add_argument("--os", help="comma-separated OS ids (default: all baselines)")
    c.add_argument("--allowlist", help="TOML allowlist")
    c.add_argument("--json", help="also write every finding to this JSON file")
    c.add_argument("--strict", action="store_true", help="warnings also fail the run")
    c.set_defaults(func=cmd_check)

    m = sub.add_parser("media", help="expand system DLLs from extracted install media")
    m.add_argument("sources", nargs="+",
                   help="extracted media directories, base first, then service packs")
    m.add_argument("--out", required=True, help="directory to write the expanded DLLs to")
    m.add_argument("--dlls", help="comma-separated DLL base names (default: built-in list)")
    m.set_defaults(func=cmd_media)

    b = sub.add_parser("baseline",
                       help="write a baseline from an ntexports dump or a directory of system DLLs")
    b.add_argument("dll_dir", nargs="?", help="directory of expanded system DLLs")
    b.add_argument("--from-dump", metavar="FILE",
                   help="EXPORTS.TXT written by ntexports.exe on the target machine")
    b.add_argument("--out", required=True, help="baselines root, e.g. tuatara/baselines")
    b.add_argument("--os-id", help="e.g. nt4-sp6a (guessed from a dump when omitted)")
    b.add_argument("--name", help='e.g. "Windows NT 4.0 SP6a"')
    b.add_argument("--version", type=_version, help="e.g. 4.0 or 3.51")
    b.add_argument("--build", type=int, help="e.g. 1381")
    b.set_defaults(func=cmd_baseline)

    s = sub.add_parser("set-version",
                       help="rewrite the PE OS/subsystem version fields (MSVC can't write < 5.01)")
    s.add_argument("files", nargs="+")
    s.add_argument("--os", type=_version, help="e.g. 3.10")
    s.add_argument("--subsystem", type=_version, help="e.g. 3.10")
    s.set_defaults(func=cmd_set_version)

    d = sub.add_parser("dump", help="print the headers, imports and exports of one file")
    d.add_argument("file")
    d.set_defaults(func=cmd_dump)
    return p


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except (pe.PEError, media.MediaError) as e:
        print(f"apiaudit: {e}", file=sys.stderr)
        return 2
