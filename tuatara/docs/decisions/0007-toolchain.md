# 0007: Toolchain

**Status:** Open, decided by the M0 spike

## Requirements

- i686 code with x87 floating point (`/arch:IA32`); SSE only in code gated by
  the OS-aware runtime check.
- PE subsystem version 3.10, so NT 3.51 loads the files. MSVC's linker won't
  write versions below 5.01, so either `lld-link` writes it or a post-link step
  patches it.
- No implicit TLS in DLLs loaded with `LoadLibrary`, and `/Zc:threadSafeInit-`,
  because thread-safe statics use implicit TLS.
- A CRT that runs on Windows 2000: the static CRT plus YY-Thunks at its
  Windows 2000 level, with `ntcompat` covering NT 4.0 and NT 3.51.

## Candidates

| | Compiler | Linker | Notes |
|---|---|---|---|
| A | Whatever roytam1's `ia32` uses today | MSVC + post-link header patch | Lowest risk: closest to what already builds |
| B | clang-cl | `lld-link /SUBSYSTEM:WINDOWS,3.10` | Full control of headers; upstream builds with MSVC |

## What roytam1's tree already does

`old-configure.in` targets XP: `WINVER` 0x0502, PE subsystem version 5.01,
`-arch:IA32` by default, `-Zc:threadSafeInit-`, MSVC 2015 Update 3 or newer
(or clang-cl), and the VS2015-style CRT as DLLs (`vcruntime140.dll`). For
Tuatara the subsystem version (to 3.10) and the CRT (to static plus thunks)
must change.

## Results so far (CI, before any run on NT)

From the first spike build on GitHub's `windows-2022` runner (MSVC 19.44,
clang 20.1, Windows SDK 10.0.26100, YY-Thunks 1.2.3):

- **Candidate A builds.** On the runner (Windows Server 2022) `hello-nt`
  passes all 21 checks, including 53-bit x87 precision, the DLL test and
  the XMM stress test. `apiaudit set-version` sets the PE versions to 3.10.
- **Candidate B** first failed to link: clang-cl's default 32-bit CPU lacks
  `cmpxchg8b`, so 64-bit interlocked operations became calls to
  `__atomic_compare_exchange_8`. Fixed with `-march=i686`, the CPU baseline.
- **Implicit TLS from the runtime.** Candidate A's `HELLODLL.DLL` has a TLS
  directory (8 bytes, 2 callbacks) from the static CRT and YY-Thunks, not
  from our code. That is the case that breaks under `LoadLibrary` before
  Vista. YY-Thunks' DLL entry point (`DllMainCRTStartupForYY_Thunks`) is
  expected to handle it on Windows 2000; NT 4.0 and NT 3.51 are unknown
  until the machines run the `loadlibrary` check.
- **Imports beyond Windows 2000's level.** With YY-Thunks at its Windows 2000
  level, the CRT still imports functions that NT 4.0 and NT 3.51 lack,
  as expected from when they were introduced (to be confirmed by the
  baselines):

  | Missing on | Functions |
  |---|---|
  | NT 4.0 and NT 3.51 | `GetFileSizeEx`, `SetFilePointerEx` |
  | NT 3.51 only | also `InitializeCriticalSectionAndSpinCount`, `IsDebuggerPresent`, `IsProcessorFeaturePresent` |

  So `ntcompat` needs thunks below YY-Thunks' lowest level, in the same
  style: `__imp_` pointers resolved at runtime with a fallback.
- `ole32.dll` is imported for `CoTaskMemAlloc` (from YY-Thunks); NT 3.51's
  32-bit OLE should provide it.

## How the spike decides

`hello-nt` is built with each candidate and run on NT 3.51 SP5, NT 4.0 SP6a and
Windows 2000 SP4, on CPUs with and without SSE. It checks CRT start-up, heap,
C++17 containers, static initialisation, threads and synchronisation, a DLL
loaded with `LoadLibrary`, the SSE probe, x87 precision, file I/O, Winsock and
a GDI window. `apiaudit` measures what each candidate still imports. The
record is updated with the results and the choice.
