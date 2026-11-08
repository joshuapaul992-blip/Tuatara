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
- **After the fix, both candidates build** and pass all 21 checks on the
  runner. clang-cl ignores `/arch:IA32` ("argument unused"); `-march=i686`
  is what keeps SSE out of its code.
- **SSE in the binaries comes only from the prebuilt static CRT** (its SSE2
  `memcpy`/math paths) and `hello-nt`'s own stress test: both candidates
  contain exactly the same XMM instructions, so the compilers generated
  none. The CRT takes those paths only when
  `IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE)` is true,
  which NT 4.0 reports as false. NT 3.51 lacks that function, so `ntcompat`'s
  replacement must answer from the SIMD policy
  ([0009](0009-winsimd.md)); that also keeps the CRT's choice consistent
  with ours everywhere.
- **Candidate B's YY-Thunks import libraries add direct ntdll imports**
  (`LdrAddRefDll`, `LdrGetProcedureAddress`, `LdrLoadDll`,
  `NtOpenKeyedEvent`). `LdrAddRefDll` and `NtOpenKeyedEvent` are expected to
  be Windows XP and later, which would stop B's files loading on Windows 2000
  and earlier. Candidate A's object-file form of YY-Thunks imports nothing
  from ntdll.

## NT 3.51 import gaps closed (ntcompat thunks)

The imports above that NT 4.0 or NT 3.51 lack now go through
[ntcompat's import thunks](../../../ntcompat/README.md): the binaries define the
`__imp_` pointers themselves, so the functions no longer appear in the import
table, and each stub calls the system's function or a fallback. `ole32.dll`
is loaded on demand instead of being a static import. In CI, candidate A
passes every check natively and with `NTCOMPAT_EMULATE=nt4` / `nt351`, and
`gen.py --check-imports` confirms none of the thunked functions is imported.

What only the real machines can still show:

- whether the NT 3.51 and NT 4.0 loaders accept these PE files at all;
- whether any *other* import is missing (the baselines from `NTEXPORT`
  answer that, and `apiaudit` then checks every build);
- whether `thread_local` in a DLL loaded with `LoadLibrary` works there
  (the `dll-tls` check; Windows Server and Wine always pass it).

## First run on real NT 3.51

Windows NT 3.51 SP5 (build 1057), 2 CPUs, candidate A from the build after
the thunks were added:

- **The NT 3.51 loader accepts the files** (PE versions patched to 3.10),
  and every import resolves.
- CRT start-up, C++17 containers, static initialisation and x87 floating
  point all pass, with 53-bit precision and IEEE-correct doubles.
- **The program then ends silently at the start of the `threads` test**,
  with no error message. The test's first action is creating a thread, so
  the suspects are callbacks that run when a thread starts: the static
  CRT's and YY-Thunks' (TLS callbacks, thread attach), which assume at
  least Windows 2000. The next spike build adds a crash report with module
  offsets and map files, progress lines from inside new threads,
  `/skip=` and `/only=`, and variant C (candidate A without YY-Thunks) to
  tell the two apart.

The map files from that build narrow it further: **without YY-Thunks the EXE
has no TLS directory at all; with it, the EXE gets one whose only callback
is YY-Thunks' (`.CRT$XLY`)**, part of its TLS emulation that works on the TEB
and loader structures, whose layouts differ before Windows 2000. Variant C
therefore now links ntcompat's own versions of the functions YY-Thunks covered
for the CRT (the "noyy" thunk set), so it runs on NT 3.51 without YY-Thunks.
If C passes the threads test where A dies, YY-Thunks' thread-start code is
the cause.

Confirmed on the NT 3.51 machine: the intermediate variant C, built before
the "noyy" set existed, stops at load with "The procedure entry point
DecodePointer could not be located in the dynamic link library KERNEL32.dll".
So NT 3.51 really lacks it, and the VS2022 runtime doesn't load there
without YY-Thunks or ntcompat's replacements. (Builds now carry their commit
ID in `BUILD.TXT`, in `hello-nt`'s first line and in the artifact name.)

With the right build (`8839292`), **variant C stops at the same place as
A**: after `threads:create1`, with no line from the new thread, no `CRASH`
line and no `FAIL`. So YY-Thunks is not the cause. What both share is the
static CRT, ntcompat and the test code, which passed NULL for
`CreateThread`'s thread-ID pointer (Windows 95 documents that as invalid;
NT 3.51 may write through it unchecked). The next build passes a real
variable everywhere, runs the tests in a supervised child process so the
exit code is always reported (NT 3.51's `cmd` has no `%ERRORLEVEL%`), and
adds `THRTEST.EXE`, which runs each way of starting a thread in its own
process with no C runtime at all.

### Result: build `e4b3d92`, variant C, on NT 3.51 SP5 (2 CPUs, WINSIMD 1.2)

**Passing a real thread-ID pointer fixed the thread crash**: NT 3.51's
`CreateThread` doesn't accept NULL there. Variant C then runs almost the whole
suite on real NT 3.51:

- CRT, C++17, x87, threads (all four start and finish with exact counts),
  TLS API, events, `cmpxchg8b`, `LoadLibrary`, files, timers, memory and a
  GDI window (`StretchDIBits`) all work; the ntcompat thunks take their NT 3.51
  fallbacks.
- **WINSIMD 1.2 is installed** (GuardFlags 0x7): the SIMD policy allows SSE and
  AVX, and the XMM stress test passes on dual-CPU NT 3.51, which WINSIMD lists
  as untested.
- The `threads` FAIL was a test bug: with `bWaitAll`, NT 3.51 returns a
  nonzero `WAIT_OBJECT_0 + n`, which is documented as success.
- Winsock: NT 3.51 has only `wsock32`, as expected; the loopback test failed
  at a step the test didn't name (it now reports the step and error).
- `dlltls` killed the process without a result: variant C has no YY-Thunks, so
  nothing sets up `thread_local` in a runtime-loaded DLL. The test now checks
  for a TLS block first and reports instead.

### Result: build `9770035`, variant C, on the same NT 3.51 machine

**24 of 26 checks pass.** `threads` passes (`WaitForMultipleObjects` returned
`WAIT_OBJECT_0 + 3`, confirming the test bug above). The two failures:

- `dll-tls`: "no TLS block for this DLL", as expected without YY-Thunks: NT
  3.51 doesn't set up implicit TLS for a DLL loaded with `LoadLibrary`.
- `winsock`: `wsock32` `socket()` fails with 10055 (`WSAENOBUFS`). Most likely
  the machine's network setup (TCP/IP not installed or not bound, or its
  driver not started); to be checked with `ping 127.0.0.1` there.

Still to run on NT 3.51: candidate A (does YY-Thunks' TLS emulation work, or
crash, on NT 3.51?), `THRTEST`, and `NTEXPORT` for the baseline.

Rule for Tuatara code: **never pass NULL for `CreateThread`'s thread-ID
pointer**, and don't rely on implicit TLS (`thread_local`,
`__declspec(thread)`) in DLLs loaded at runtime unless something provides it.

**Current lean: candidate A** (MSVC, as upstream builds, with
`apiaudit set-version`). It is confirmed only after the runs on the real
machines; B stays in the spike to compare.

## How the spike decides

`hello-nt` is built with each candidate and run on NT 3.51 SP5, NT 4.0 SP6a and
Windows 2000 SP4, on CPUs with and without SSE. It checks CRT start-up, heap,
C++17 containers, static initialisation, threads and synchronisation, a DLL
loaded with `LoadLibrary`, the SSE probe, x87 precision, file I/O, Winsock and
a GDI window. `apiaudit` measures what each candidate still imports. The
record is updated with the results and the choice.
