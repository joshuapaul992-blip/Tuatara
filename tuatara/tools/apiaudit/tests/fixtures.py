# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Build small, real PE files for the tests with clang + lld-link.

Nothing here runs the binaries; they only need correct import, export, TLS
and version tables. Tests skip when the LLVM tools are missing.
"""

import os
import shutil
import subprocess
import tempfile

TOOLS = ["clang", "lld-link", "llvm-dlltool", "llvm-rc"]

# Fake "system" DLLs, standing in for an old OS.
FAKE_SYSTEM = {
    "kernel32": {
        "c": "int GetTickCount(void){return 0;} void Sleep(int x){(void)x;} "
             "int Ord5(void){return 5;}",
        "def": ["GetTickCount", "Sleep", "Ord5 @5 NONAME"],
        # Passed as /export: flags; lld-link decorates forwarders written in
        # a .def file for x86 ("_ntdll.X"), which real system DLLs never do.
        "forwarders": ["HeapAlloc=ntdll.RtlAllocateHeap", "Broken=ntdll.RtlNope"],
        "version": "4,0,1381,300",
    },
    "ntdll": {"c": "int RtlAllocateHeap(void){return 0;}", "def": ["RtlAllocateHeap"]},
    "user32": {"c": "int MessageBoxA(void){return 0;}", "def": ["MessageBoxA"]},
}

# Import libraries for the "modern" API surface the app is linked against.
MODERN_IMPORTS = {
    "kernel32": ["GetTickCount", "GetTickCount64", "HeapAlloc", "Broken",
                 "Ord5 @5 NONAME", "Ord99 @99 NONAME"],
    "user32": ["MessageBoxA", "MessageBoxW"],
    "missing": ["X"],
}

APP_C = r"""
int GetTickCount(void); long long GetTickCount64(void); void *HeapAlloc(void);
int Broken(void); int Ord5(void); int Ord99(void);
int MessageBoxA(void); int MessageBoxW(void);
int LocalFn(void); int X(void);
void *__stdcall __delayLoadHelper2(void *d, void *f) { (void)d; (void)f; return 0; }
int entry(void) {
  return GetTickCount() + (int)GetTickCount64() + (int)(long)HeapAlloc() + Broken()
       + Ord5() + Ord99() + MessageBoxA() + MessageBoxW() + LocalFn() + X();
}
"""

# An app-local DLL with an implicit TLS directory.
APPLOCAL_C = r"""
int _tls_index = 0;
static char tls_data[16];
struct { void *start, *end; int *index; void *callbacks; unsigned zero_fill, flags; }
  _tls_used = { tls_data, tls_data + 16, &_tls_index, 0, 0, 0 };
int LocalFn(void) { return 1; }
"""

TRIVIAL_C = "int entry(void) { return 0; }"


def have_tools():
    return all(shutil.which(t) for t in TOOLS)


class Fixture:
    """Builds everything once into a temporary directory."""

    def __init__(self):
        self.root = tempfile.mkdtemp(prefix="apiaudit-test-")
        self.system = os.path.join(self.root, "fakesys")
        self.dist = os.path.join(self.root, "dist")
        self.libs = os.path.join(self.root, "libs")
        for d in (self.system, self.dist, self.libs):
            os.makedirs(d)
        self._build()

    def cleanup(self):
        shutil.rmtree(self.root, ignore_errors=True)

    def _run(self, *cmd):
        subprocess.run(cmd, check=True, cwd=self.root,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def _write(self, path, text):
        with open(path, "w") as f:
            f.write(text)
        return path

    def _compile(self, name, source):
        src = self._write(os.path.join(self.root, name + ".c"), source)
        obj = os.path.join(self.root, name + ".obj")
        self._run("clang", "--target=i686-pc-windows-msvc", "-O1", "-ffreestanding",
                  "-fno-stack-protector", "-c", src, "-o", obj)
        return obj

    def _def(self, path, dll, exports):
        return self._write(path, f"LIBRARY {dll}\nEXPORTS\n" +
                           "".join(f"  {e}\n" for e in exports))

    def _build(self):
        for name, spec in FAKE_SYSTEM.items():
            obj = self._compile("sys_" + name, spec["c"])
            deffile = self._def(os.path.join(self.root, f"sys_{name}.def"),
                                name + ".dll", spec["def"])
            inputs = [obj] + [f"/export:{fwd}" for fwd in spec.get("forwarders", [])]
            if "version" in spec:
                rc = self._write(os.path.join(self.root, f"sys_{name}.rc"),
                                 f"1 VERSIONINFO\nFILEVERSION {spec['version']}\n"
                                 "BEGIN\nEND\n")
                res = os.path.join(self.root, f"sys_{name}.res")
                self._run("llvm-rc", "/FO", res, rc)
                inputs.append(res)
            self._run("lld-link", "/dll", "/noentry", "/nodefaultlib", "/machine:x86",
                      f"/def:{deffile}", f"/out:{os.path.join(self.system, name + '.dll')}",
                      *inputs)

        libs = []
        for name, exports in MODERN_IMPORTS.items():
            deffile = self._def(os.path.join(self.root, f"imp_{name}.def"),
                                name + ".dll", exports)
            lib = os.path.join(self.libs, name + ".lib")
            self._run("llvm-dlltool", "-m", "i386", "-d", deffile, "-l", lib)
            libs.append(lib)

        applocal_obj = self._compile("applocal", APPLOCAL_C)
        applocal_def = self._def(os.path.join(self.root, "applocal.def"),
                                 "applocal.dll", ["LocalFn"])
        self._run("lld-link", "/dll", "/noentry", "/nodefaultlib", "/machine:x86",
                  f"/def:{applocal_def}", f"/out:{os.path.join(self.dist, 'applocal.dll')}",
                  f"/implib:{os.path.join(self.libs, 'applocal.lib')}", applocal_obj)
        libs.append(os.path.join(self.libs, "applocal.lib"))

        app_obj = self._compile("app", APP_C)
        self._run("lld-link", "/entry:entry", "/nodefaultlib", "/machine:x86",
                  "/subsystem:console,3.10", "/delayload:user32.dll",
                  f"/out:{os.path.join(self.dist, 'app.exe')}", app_obj, *libs)

        new_obj = self._compile("new", TRIVIAL_C)
        self._run("lld-link", "/entry:entry", "/nodefaultlib", "/machine:x86",
                  "/subsystem:console,6.0", f"/out:{os.path.join(self.dist, 'new.exe')}",
                  new_obj)
