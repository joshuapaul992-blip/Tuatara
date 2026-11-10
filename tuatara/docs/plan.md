# Tuatara plan

Tuatara is a Basilisk-style browser for Windows NT 3.51, NT 4.0 and
Windows 2000, built on the Quentix engine (UXP via roytam1's `ia32` branch).
The model is Supermium, which keeps current Chromium running on Windows XP and
Vista and brings back classic UI options. Tuatara does the same for UXP, and
goes further back.

Decisions are recorded in [decisions/](decisions/). This file is the living
overview; update it when a milestone's scope changes.

## Pillars

1. **Track upstream quickly.** Merge roytam1's `ia32` and release within days,
   weekly when upstream moves.
2. **One build for every Windows.** One i686 binary runs on NT 3.51 through
   Windows 11 and picks code paths at runtime.
3. **Basilisk-style UI with classic presets.** Australis by default, plus
   Classic Tabs, Compact, System Colors and Dark.
4. **Site compatibility.** A Firefox-compatible user agent by default, with
   per-site overrides.
5. **Extensions.** XUL extensions; WebExtensions as inherited from roytam1's
   tree (to be decided).
6. **Honest about limits.** An "About this machine" page explains what works
   on this machine and why.

## How it works

- **No system changes.** No extended kernels and no replaced system DLLs.
  Missing APIs are filled in by `ntcompat`, a statically linked layer that
  looks functions up at runtime and falls back to its own implementation.
- **The API audit is the guardrail.** `apiaudit` checks every built binary
  against export lists taken from the real systems. CI fails on any import a
  target OS lacks unless it is allowlisted with a fallback.
- **CPU features are checked against the OS.** SSE is used only where
  something saves the registers at thread switches: the OS on Windows 2000
  and later, or the optional [WINSIMD](https://github.com/ages2001/WINSIMD)
  1.2 driver on NT 3.51 and NT 4.0. An SEH-guarded probe plus WINSIMD's status
  key decide this once, and every library's CPU detection goes through that
  decision. CPUID, or the probe alone, is not enough
  ([0009](decisions/0009-winsimd.md)).
- **Software graphics.** Skia/Cairo with the basic compositor, drawn to the
  window with `StretchDIBits`. No Direct2D, DirectWrite or Direct3D.
- **Fonts.** GDI on Windows 2000; FreeType with bundled fonts on NT 4.0 and
  NT 3.51, which lack OpenType support and `AddFontMemResourceEx`.
- **Toolchain.** MSVC (as upstream expects) or clang-cl, with `/arch:IA32`,
  `/Zc:threadSafeInit-`, no implicit TLS in DLLs, a static CRT plus YY-Thunks
  at its Windows 2000 level, and PE subsystem version 3.10. M0 decides which.

## Milestones

| | Milestone | Exit criteria |
|---|---|---|
| **M0** | Toolchain spike and guardrails | `tuatara` branch and docs; baselines for the three systems; `apiaudit` in CI with a first gap report against roytam1's XP build; `hello-nt` passes on all three systems with and without SSE; toolchain decided |
| **M1** | Foundations on all three systems | NSS completes a TLS 1.3 handshake; the JS shell passes jit-tests in interpreter mode; ICU and the allocator self-test |
| **M2** | Headless engine on Windows 2000 | `libxul` loads; xpcshell tests run |
| **M3** | First browser on Windows 2000 | Front end in `application/tuatara/`, rebranded; classic CSS; software compositing; GDI fonts; WinMM audio; HTTPS sites load |
| **M3.5** | Front-end speed check | Windows 2000 with the JIT off, as a stand-in for NT 4.0; pick the default UI preset per OS |
| **M4** | Windows NT 4.0 | FreeType backend and font pack; opaque panels; multi-monitor stubs; `getaddrinfo` fallback; JIT with WINSIMD 1.2 passes jit-tests and the XMM stress test, single- and dual-CPU |
| **M5** | Windows NT 3.51 (stretch) | Winsock 1.1 shim; entropy without CryptoAPI; OLE fallbacks; portable profile |
| **M6** | Release pipeline | Weekly builds; one installer for NT 3.51 to Windows 11; About This Machine; low-memory defaults |

## M0 in detail

| Step | Status |
|---|---|
| M0.1 `tuatara` branch, README, docs, decision records | Done |
| M0.3 `apiaudit`: imports, delay imports, ordinals, forwarders, TLS, header versions, allowlist | Done |
| M0.2 Baselines for `nt351-sp5`, `nt4-sp6a`, `w2k-sp4` | Tooling done: `NTEXPORT.EXE` on each real machine, or `apiaudit media` from install media. Needs runs on the machines |
| First gap report against roytam1's XP build of Serpent | Needs the baselines |
| M0.4 Test lab | Primary: the real NT 3.51 / NT 4.0 / 2000 machines, using [`RUNME.TXT`](../spike/RUNME.TXT). QEMU here later, once install media is available |
| M0.5 Toolchain spike: `hello-nt`, both candidates built in CI | Built; needs runs on the machines |
| M0.6 CI: `apiaudit` tests on Linux; Windows spike build | Done |

## M1 in detail

| Step | Status |
|---|---|
| NSPR 4.24 + NSS 3.48.6 built for i686 x87, static CRT (CI, [`tuatara/m1`](../m1/build-nss.sh)) | Done |
| TLS 1.3 handshake (`tstclnt` against OpenSSL), natively and with `NTCOMPAT_EMULATE=nt351` | Passing in CI |
| ntcompat linked into every NSPR/NSS file; imports limited to `kernel32`, `advapi32`, `winmm` | Done: Winsock binds at runtime to `ws2_32`, or `wsock32` on NT 3.51 |
| NSS randomness on NT 3.51 (`RtlGenRandom` fallback: CryptoAPI, then ntcompat's pool) | Done |
| NSS SIMD (AES-NI, PCLMUL, AVX, SSSE3, mpi SSE2) follows the SIMD policy | Done |
| NSS on real NT 3.51 (`NSSTEST.CMD`) | To run |
| SpiderMonkey shell (`js.exe`), JIT gated by the SIMD policy, jit-tests in interpreter mode | Built in CI ([`build-js.sh`](../m1/build-js.sh)): `js.exe` plus `mozglue`, `nspr4`, `plc4`, `plds4` and `icu78` DLLs, PE 3.10, no thunked function imported by any of them (PSAPI's `GetProcessMemoryInfo` is lazy-bound). Runs natively and with `NTCOMPAT_EMULATE=nt351`, including [`JSTEST.JS`](../m1/JSTEST.JS) (14 checks: x87 arithmetic, GC load, ICU). jit-tests `basic` in interpreter mode: 1330 pass, 0 unexpected failures; 7 excluded for tree reasons (harness without `skip-if`, parser positions, `toSource`), listed with reasons in [`jit-tests-known-failures.txt`](../m1/jit-tests-known-failures.txt), still to be compared with a Linux build. Fixed on the way: ICU wasn't configured or linked for standalone builds, shared-only JS on Windows, C++17 for ICU, MozillaBuild's MSYS mangling paths, the tree's `LINK` make variable overriding ours (now `_LINK_`), `moz_malloc_size_of` missing under the static CRT, CRLF checkouts |
| SpiderMonkey on real NT 3.51 (`JSTEST.CMD`) | To run |
| ICU and the allocator (mozjemalloc) self-tests | To do |

## Risks

| Risk | Mitigation |
|---|---|
| Upstream starts using newer Win32 APIs or C++ features | `apiaudit` in CI on every merge; small, runtime-gated patches |
| The NT 3.51 loader rejects modern PE files | Tested first, in the M0 spike |
| JavaScript speed without the JIT | Optional WINSIMD 1.2 brings the JIT to NT 4.0 and 3.51; otherwise documented, with Classic Tabs as the fallback default preset |
| WINSIMD is a young kernel driver | Optional only; Tuatara's own stress testing before it's advertised; status key read defensively |
| RAM | Low-memory defaults; Large Address Aware for NT 4.0 with `/3GB` |
| Font backend port | Reuse UXP's FreeType font list (the Android path) |
| Slow testing on emulators | Capability-emulation mode on modern Windows for most work; VMs to confirm |
| Old, unpatched host OS | Clear security notice; recommend use behind NAT |
