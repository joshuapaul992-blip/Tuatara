# Supported systems

Baseline service packs are fixed, so the API surface is predictable. Newer
components (Internet Explorer updates, redistributables) may be used when
present, but are never required.

| | Windows NT 3.51 | Windows NT 4.0 | Windows 2000 |
|---|---|---|---|
| Required service pack | SP5 | SP6a | SP4 |
| Baseline id | `nt351-sp5` | `nt4-sp6a` | `w2k-sp4` |
| Version / build | 3.51 / 1057 | 4.0 / 1381 | 5.0 / 2195 |
| Priority | Stretch goal (M5) | M4 | First target (M3) |
| Internet Explorer needed | No | No | No |

## CPU

- **Minimum:** Pentium Pro / Pentium II / Athlon (i686, with `cmov` and `cmpxchg8b`).
  The AMD K6 family lacks `cmov` and would need a separate i586 build.
- **SSE/SSE2:** used only when the CPU has it *and* the OS saves SSE registers.
  Windows 2000 does; NT 4.0 and NT 3.51 do not, so on them the browser uses x87
  floating point everywhere, whatever the CPU.
- **Architectures:** x86 only. The Alpha, MIPS and PowerPC versions of NT are
  out of scope; no current compiler targets them.

## What differs by system

| Feature | 2000 | NT 4.0 | NT 3.51 |
|---|---|---|---|
| JavaScript JIT | Yes, with SSE2 | No (interpreter) | No (interpreter) |
| WebAssembly | Yes, with the JIT | No | No |
| WebGL | No | No | No |
| Font rendering | GDI | FreeType + bundled fonts | FreeType + bundled fonts |
| Transparent popups and panels | Yes | Opaque fallback | Opaque fallback |
| Networking | Winsock 2, IPv4 | Winsock 2, IPv4 | Winsock 1.1 shim, IPv4 |
| Audio | WinMM | WinMM | WinMM |
| Video (VP8/VP9) | Software decode | Software decode | Software decode |
| Profile location | User profile | User profile | Next to the program (portable) |

## Memory

Plan for 512 MB of RAM or more; 256 MB is the practical floor for light
browsing. A low-memory preference profile is chosen automatically from the
installed RAM.

## Expectations

Text-heavy sites (Wikipedia, forums, news, documentation) should be usable
everywhere. Sites built as large JavaScript applications (video sites, web mail,
social media) need the JIT, so on NT 4.0 and NT 3.51 expect them to be slow or
unusable.
