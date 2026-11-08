# M0 toolchain spike

Small programs that answer, on real NT 3.51, NT 4.0 and Windows 2000 machines,
which toolchain can build Tuatara ([decision 0007](../docs/decisions/0007-toolchain.md)).

| Program | What it does |
|---|---|
| `HELLO-NT.EXE` + `HELLODLL.DLL` | Checks C/C++ runtime start-up, C++17 containers, static initialisation, x87 precision, threads and synchronisation, a DLL loaded with `LoadLibrary`, files, Winsock, the [SIMD policy](../../ntcompat/cpu/simd_policy.h) with an XMM stress test, timers, memory, and a GDI window. Writes `HELLO-NT.TXT`. |
| `NTEXPORT.EXE` | Writes `EXPORTS.TXT`: the export tables of the system DLLs, for [apiaudit baselines](../tools/apiaudit/README.md). No C runtime; imports only NT 3.1 kernel32 functions. |

## Builds

The [Tuatara toolchain spike](../../.github/workflows/tuatara-spike.yml)
workflow builds both candidates on GitHub's Windows runners and publishes them
as the `tuatara-spike` artifact:

| Folder | Candidate |
|---|---|
| `A-MSVC` | MSVC `cl` + `link`, static CRT, YY-Thunks (Windows 2000 level); PE versions set to 3.10 afterwards with `apiaudit set-version` |
| `B-CLANG` | `clang-cl` + `lld-link /subsystem:console,3.10`, static CRT, YY-Thunks import libraries when available |

Both use `/arch:IA32` (x87 only) and `/Zc:threadSafeInit-`. Each folder also has
`IMPORTS.TXT`, the imports and PE headers of every file.

To build locally from an x86 Visual Studio prompt, see the comments at the top
of [`build.cmd`](build.cmd).

## Running on the target machines

[`RUNME.TXT`](RUNME.TXT) is the step-by-step version that ships with the
binaries. In short: run `NTEXPORT` once per machine, run `HELLO-NT` from each
folder, and bring back `EXPORTS.TXT` and both `HELLO-NT.TXT` files, along with
the machine's CPU, RAM, CPU count and whether WINSIMD is installed.

## Smoke tests without NT

`HELLO-NT` also runs on modern Windows and under Wine. Setting
`NTCOMPAT_EMULATE=nt4` (or `nt351`, `w2k`, `xp`) makes the SIMD policy decide
as on that system.
