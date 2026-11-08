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

## How the spike decides

`hello-nt` is built with each candidate and run on NT 3.51 SP5, NT 4.0 SP6a and
Windows 2000 SP4, on CPUs with and without SSE. It checks CRT start-up, heap,
C++17 containers, static initialisation, threads and synchronisation, a DLL
loaded with `LoadLibrary`, the SSE probe, x87 precision, file I/O, Winsock and
a GDI window. `apiaudit` measures what each candidate still imports. The
record is updated with the results and the choice.
