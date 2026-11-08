# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import contextlib
import io
import os
import shutil
import tempfile
import unittest

from apiaudit import audit, baseline, cli, media, pe
from apiaudit.tests.fixtures import Fixture, have_tools

FIX = None


def setUpModule():
    global FIX
    if have_tools():
        FIX = Fixture()


def tearDownModule():
    if FIX:
        FIX.cleanup()


def needs_fixture(cls):
    return unittest.skipUnless(have_tools(), "clang/lld-link/llvm-dlltool/llvm-rc not found")(cls)


def dist(name):
    return os.path.join(FIX.dist, name)


@needs_fixture
class PeParsing(unittest.TestCase):
    def test_static_and_delay_imports(self):
        img = pe.parse(dist("app.exe"))
        self.assertFalse(img.is_dll)
        self.assertEqual(img.subsystem_version, (3, 10))
        by_dll = {i.dll.lower(): i for i in img.imports}
        k32 = by_dll["kernel32.dll"]
        self.assertFalse(k32.delay)
        names = {s.name for s in k32.symbols if s.name}
        self.assertEqual(names, {"GetTickCount", "GetTickCount64", "HeapAlloc", "Broken"})
        self.assertEqual({s.ordinal for s in k32.symbols if s.name is None}, {5, 99})
        u32 = by_dll["user32.dll"]
        self.assertTrue(u32.delay)
        self.assertEqual({s.name for s in u32.symbols}, {"MessageBoxA", "MessageBoxW"})

    def test_exports_forwarders_and_version(self):
        img = pe.parse(os.path.join(FIX.system, "kernel32.dll"))
        self.assertTrue(img.is_dll)
        self.assertEqual(img.file_version, "4.0.1381.300")
        by_name = {e.name: e for e in img.exports if e.name}
        self.assertEqual(by_name["HeapAlloc"].forwarder, "ntdll.RtlAllocateHeap")
        self.assertIsNone(by_name["GetTickCount"].forwarder)
        self.assertIn(5, {e.ordinal for e in img.exports if e.name is None})

    def test_tls_directory(self):
        self.assertIsNotNone(pe.parse(dist("applocal.dll")).tls)
        self.assertIsNone(pe.parse(dist("app.exe")).tls)

    def test_truncated_files_raise_pe_error_only(self):
        with open(dist("app.exe"), "rb") as f:
            data = f.read()
        for cut in list(range(0, 1024, 7)) + list(range(1024, len(data), 97)):
            try:
                pe.parse("cut", data[:cut])
            except pe.PEError:
                pass

    def test_corrupted_bytes_raise_pe_error_only(self):
        with open(dist("app.exe"), "rb") as f:
            data = bytearray(f.read())
        for off in range(0x40, min(len(data), 0x1400), 13):
            bad = bytearray(data)
            bad[off] ^= 0xFF
            try:
                pe.parse("bad", bytes(bad))
            except pe.PEError:
                pass

    def test_not_a_pe(self):
        with self.assertRaises(pe.PEError):
            pe.parse("x", b"hello world")


@needs_fixture
class Auditing(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.base_dir = os.path.join(self.tmp, "baselines")
        baseline.write_baseline(FIX.system, os.path.join(self.base_dir, "fake-nt4"),
                                "fake-nt4", "Fake NT 4.0", (4, 0), 1381, log=lambda *_: None)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def run_audit(self, allow=None):
        bases = baseline.load_all(self.base_dir)
        return audit.Auditor(bases, allow).run([FIX.dist]), bases

    def keys(self, findings):
        return {(f.check, os.path.basename(f.file), f.dll, f.function, f.kind)
                for f in findings}

    def test_baseline_roundtrip(self):
        b = baseline.load_os(os.path.join(self.base_dir, "fake-nt4"))
        self.assertEqual(b.version, (4, 0))
        k32 = b.find_dll("KERNEL32.DLL")
        self.assertEqual(k32.file_version, "4.0.1381.300")
        self.assertEqual(k32.by_name["HeapAlloc"], "ntdll.RtlAllocateHeap")
        self.assertIn(5, k32.by_ordinal)

    def test_findings(self):
        findings, _ = self.run_audit()
        got = self.keys(findings)
        expected = {
            ("missing-function", "app.exe", "kernel32.dll", "GetTickCount64", "static"),
            ("forwarder-broken", "app.exe", "kernel32.dll", "Broken", "static"),
            ("ordinal-import", "app.exe", "kernel32.dll", "#5", "static"),
            ("ordinal-import", "app.exe", "kernel32.dll", "#99", "static"),
            ("missing-ordinal", "app.exe", "kernel32.dll", "#99", "static"),
            ("missing-function", "app.exe", "user32.dll", "MessageBoxW", "delay"),
            ("missing-dll", "app.exe", "missing.dll", None, "static"),
            ("tls-directory", "applocal.dll", None, None, None),
            ("subsystem-version", "applocal.dll", None, None, None),
            ("os-version", "applocal.dll", None, None, None),
            ("subsystem-version", "new.exe", None, None, None),
            ("os-version", "new.exe", None, None, None),
        }
        self.assertEqual(got, expected)
        # Resolved fine: a working forwarder, an app-local DLL, existing names.
        self.assertFalse(any(f.function in ("HeapAlloc", "GetTickCount", "LocalFn",
                                            "MessageBoxA") for f in findings))

    def test_report_and_exit_code(self):
        findings, bases = self.run_audit()
        text, code = audit.summarise(findings, bases, [])
        self.assertEqual(code, 1)
        self.assertIn("GetTickCount64", text)
        self.assertIn("[delay]", text)

    def test_allowlist(self):
        path = os.path.join(self.tmp, "allow.toml")
        with open(path, "w") as f:
            f.write('[[allow]]\ndll = "KERNEL32.dll"\nfunction = "GetTickCount*"\n'
                    'reason = "ntcompat thunk"\n\n'
                    '[[allow]]\ncheck = "missing-dll"\ndll = "never.dll"\nreason = "stale"\n')
        allow = audit.load_allowlist(path)
        findings, bases = self.run_audit(allow)
        gtc = [f for f in findings if f.function == "GetTickCount64"]
        self.assertEqual([f.allowed_by for f in gtc], [1])
        text, code = audit.summarise(findings, bases, allow)
        self.assertNotIn("GetTickCount64", text.split("note:")[0])
        self.assertIn("entry 2 matched nothing", text)
        self.assertEqual(code, 1)  # other errors are still open

    def test_allowlist_requires_reason(self):
        path = os.path.join(self.tmp, "bad.toml")
        with open(path, "w") as f:
            f.write('[[allow]]\nfunction = "X"\n')
        with self.assertRaises(SystemExit):
            audit.load_allowlist(path)

    def test_cli_check(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = cli.main(["check", "--baselines", self.base_dir, FIX.dist,
                             "--json", os.path.join(self.tmp, "f.json")])
        self.assertEqual(code, 1)
        self.assertIn("== fake-nt4", out.getvalue())
        self.assertTrue(os.path.exists(os.path.join(self.tmp, "f.json")))

    def test_clean_image_passes(self):
        bases = baseline.load_all(self.base_dir)
        clean = os.path.join(FIX.system, "ntdll.dll")
        findings = audit.Auditor(bases).run([clean])
        # Only the fake DLL's own (lld default 6.0) header versions are flagged.
        self.assertEqual({f.check for f in findings}, {"subsystem-version", "os-version"})


SAMPLE_DUMP = (
    "TUATARA-NTEXPORTS\t1\n"
    "os\t4\t0\t1381\tService Pack 6\n"
    "dll\tkernel32.dll\t4.0.1381.300\t14c\n"
    "e\t1\tAddAtomA\t\n"
    "e\t2\tHeapAlloc\tNTDLL.RtlAllocateHeap\n"
    "e\t7\t\t\n"
    "dll\tntdll.dll\t4.0.1381.300\t14c\n"
    "e\t1\tRtlAllocateHeap\t\n"
    "missing\tmsimg32.dll\n"
    "end\n")


class Dumps(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def write(self, text):
        path = os.path.join(self.tmp, "EXPORTS.TXT")
        with open(path, "w", newline="") as f:
            f.write(text)
        return path

    def test_from_dump_guesses_os_and_round_trips(self):
        os_id = baseline.write_baseline_from_dump(self.write(SAMPLE_DUMP), self.tmp,
                                                  log=lambda *_: None)
        self.assertEqual(os_id, "nt4-sp6a")
        b = baseline.load_os(os.path.join(self.tmp, "nt4-sp6a"))
        self.assertEqual((b.version, b.build, b.name), ((4, 0), 1381, "Windows NT 4.0 SP6a"))
        k32 = b.find_dll("kernel32.dll")
        self.assertEqual(k32.by_name["HeapAlloc"], "NTDLL.RtlAllocateHeap")
        self.assertIn(7, k32.by_ordinal)
        self.assertEqual(k32.file_version, "4.0.1381.300")

    def test_dump_with_crlf_line_endings(self):
        path = self.write(SAMPLE_DUMP.replace("\n", "\r\n"))
        self.assertEqual(baseline.write_baseline_from_dump(path, self.tmp, log=lambda *_: None),
                         "nt4-sp6a")

    def test_truncated_dump_is_rejected(self):
        with self.assertRaises(ValueError):
            baseline.parse_dump(self.write(SAMPLE_DUMP.replace("end\n", "")))

    def test_unknown_os_needs_explicit_id(self):
        text = SAMPLE_DUMP.replace("os\t4\t0\t1381\tService Pack 6", "os\t5\t1\t2600\tService Pack 3")
        with self.assertRaises(ValueError):
            baseline.write_baseline_from_dump(self.write(text), self.tmp, log=lambda *_: None)
        self.assertEqual(baseline.write_baseline_from_dump(
            self.write(text), self.tmp, os_id="xp-sp3", os_name="Windows XP SP3",
            log=lambda *_: None), "xp-sp3")

    def test_not_a_dump(self):
        with self.assertRaises(ValueError):
            baseline.parse_dump(self.write("hello\n"))


@needs_fixture
class SetVersion(unittest.TestCase):
    def test_rewrites_versions_and_checksum(self):
        tmp = tempfile.mkdtemp()
        try:
            path = os.path.join(tmp, "new.exe")
            shutil.copy(dist("new.exe"), path)
            old_os, old_ss = pe.set_versions(path, (3, 10), (3, 10))
            self.assertEqual((old_os, old_ss), ((6, 0), (6, 0)))
            img = pe.parse(path)
            self.assertEqual((img.os_version, img.subsystem_version), ((3, 10), (3, 10)))
            with open(path, "rb") as f:
                data = f.read()
            opt = int.from_bytes(data[0x3C:0x40], "little") + 24
            stored = int.from_bytes(data[opt + 64:opt + 68], "little")
            self.assertNotEqual(stored, 0)
            self.assertEqual(stored, pe.pe_checksum(data, opt + 64))
        finally:
            shutil.rmtree(tmp)


def szdd_compress_literals(data: bytes) -> bytes:
    """Encode *data* as SZDD using only literals (valid, just not smaller)."""
    out = bytearray(media.SZDD_MAGIC + b"A_" + len(data).to_bytes(4, "little"))
    for i in range(0, len(data), 8):
        chunk = data[i:i + 8]
        out.append((1 << len(chunk)) - 1)
        out += chunk
    return bytes(out)


class Media(unittest.TestCase):
    def test_szdd_literals(self):
        data = bytes(range(256)) * 3
        self.assertEqual(media.expand_szdd(szdd_compress_literals(data)), data)

    def test_szdd_back_reference(self):
        # "ABCD" as literals at window position 4080, then a 4-byte copy of it.
        header = media.SZDD_MAGIC + b"A_" + (8).to_bytes(4, "little")
        body = bytes([0x0F]) + b"ABCD" + bytes([0xF0, 0xF1])
        self.assertEqual(media.expand_szdd(header + body), b"ABCDABCD")

    def test_szdd_rejects_garbage(self):
        with self.assertRaises(media.MediaError):
            media.expand_szdd(b"not szdd")

    def test_collect_overrides_in_order(self):
        tmp = tempfile.mkdtemp()
        try:
            base, sp, out = (os.path.join(tmp, d) for d in ("base/I386", "sp", "out"))
            os.makedirs(base)
            os.makedirs(sp)
            with open(os.path.join(base, "KERNEL32.DL_"), "wb") as f:
                f.write(szdd_compress_literals(b"MZ-base-kernel32"))
            with open(os.path.join(base, "USER32.DL_"), "wb") as f:
                f.write(szdd_compress_literals(b"MZ-base-user32"))
            with open(os.path.join(sp, "kernel32.dll"), "wb") as f:
                f.write(b"MZ-sp-kernel32")
            found, missing = media.collect([os.path.dirname(base), sp], out,
                                           dlls=["kernel32", "user32", "gdi32"],
                                           log=lambda *_: None)
            self.assertEqual(found, ["kernel32", "user32"])
            self.assertEqual(missing, ["gdi32"])
            with open(os.path.join(out, "kernel32.dll"), "rb") as f:
                self.assertEqual(f.read(), b"MZ-sp-kernel32")
            with open(os.path.join(out, "user32.dll"), "rb") as f:
                self.assertEqual(f.read(), b"MZ-base-user32")
        finally:
            shutil.rmtree(tmp)


if __name__ == "__main__":
    unittest.main()
