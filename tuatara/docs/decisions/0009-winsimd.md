# 0009: Optional WINSIMD support

**Status:** Accepted

## Background

Windows NT 3.51 and NT 4.0 don't enable SSE, and don't save the SSE (XMM)
registers when they switch threads. Windows 2000 and XP save XMM but never the
upper halves of the AVX (YMM) registers.

[WINSIMD](https://github.com/ages2001/WINSIMD) is a third-party kernel driver
that enables SSE through AVX-512 on these systems. Since version 1.2 it also
saves, at each thread switch, the registers the OS doesn't. It hooks the
CPU's lazy FPU-switch trap (#NM) and doesn't depend on a kernel build. It
reports what it protects in a volatile registry key that exists only while
the 1.2 driver runs:

    HKLM\SYSTEM\CurrentControlSet\Services\winsimd\Runtime
        GuardFlags   1 = active, 2 = XMM saved, 4 = YMM saved, 8 = ZMM saved

## Decision

1. **WINSIMD is optional and never required.** Without it, NT 3.51 and NT 4.0
   run the x87 code paths with the JavaScript interpreter, as planned. With
   it, the same binary turns on SSE2 paths, the JIT and WebAssembly
   (see [0005](0005-single-binary.md)).
2. **Probing alone is not trusted on old systems.** Executing an SSE
   instruction only shows that something has enabled SSE, not that anything
   saves the registers. WINSIMD 1.1, for example, enabled SSE without saving
   it, and threads corrupted each other's registers. The policy is:

   | OS | SSE / SSE2 allowed when | AVX allowed when |
   |---|---|---|
   | NT 3.51, NT 4.0 | The probe passes **and** `GuardFlags & 3 == 3` (active, XMM saved) | `GuardFlags & 7 == 7` (active, XMM and YMM saved) |
   | Windows 2000, XP | The probe passes (the OS saves XMM) | `GuardFlags & 5 == 5` (active, YMM saved; these systems never save YMM) |
   | Vista and later | Normal checks | Normal checks (the OS enables and saves YMM from Windows 7 SP1) |

   AVX-512 is never used.
3. **One decision point.** `ntcompat`'s CPU policy computes this once at
   start-up, and every library with its own CPU detection is routed through
   it (SpiderMonkey, Skia, pixman, libjpeg-turbo, libvpx, ffvpx, Opus, NSS).
   Libraries that test the OS-enabled AVX flag themselves would otherwise use
   AVX unprotected on Windows 2000 and XP.
4. **Not bundled.** WINSIMD is GPLv3. Tuatara only reads its registry key, so
   Tuatara's license is unaffected. Users install WINSIMD from its own
   releases; "About this machine" shows its status and links to it when an
   SSE2 CPU runs without it.
5. **Minimum version 1.2.** The `Runtime` key only exists from 1.2, so older
   versions are treated as absent.

## Risks

- **Kernel driver:** a bug means a blue screen, not a crash of the browser.
  At the time of this decision WINSIMD 1.2 was days old. Its author lists it
  as tested in QEMU only: NT 4.0 single-CPU, NT 3.1 single- and dual-CPU, and
  not NT 3.5x or real hardware. Tuatara advertises WINSIMD support only after
  its own testing (below).
- **NT 3.5x thread exit:** NT 3.5x doesn't notify drivers when threads exit,
  so WINSIMD reuses its least recently used save slot (1,024 in all). A thread
  idle long enough could lose its saved XMM state. On x86 Windows, XMM
  registers are scratch across calls, so a thread blocked in a wait shouldn't
  hold live values; the risk is small but not zero. MXCSR would reset to its
  default.
- **Interface stability:** the `Runtime` key is WINSIMD's documented status
  interface, but not promised as stable. Ask the author before relying on it,
  and fall back to "absent" for any value we don't recognise.

## Testing before advertising support

On NT 4.0 + WINSIMD 1.2, single- and dual-CPU, and NT 3.51 + WINSIMD 1.2
single-CPU, in QEMU and then on real hardware:

- SpiderMonkey jit-tests with the JIT on;
- a multithreaded XMM stress test (in the style of WINSIMD's `xmmstrs`) run
  alongside the browser;
- an hour of browsing with media decoding running.
