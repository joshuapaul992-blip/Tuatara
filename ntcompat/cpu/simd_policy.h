/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * ntcompat SIMD policy: which SIMD instruction sets this process may use.
 *
 * A CPU feature is usable only if the CPU has it *and* something saves its
 * registers when threads switch. Windows 2000 and later save XMM; Windows 7
 * SP1 and later save YMM. On NT 3.51 and NT 4.0, and for YMM on Windows 2000
 * and XP, only the optional WINSIMD 1.2 driver does, and it says so in its
 * registry status key. See tuatara/docs/decisions/0009-winsimd.md.
 *
 * Every library that picks SIMD code paths at runtime must ask this policy
 * instead of trusting CPUID (or an instruction probe) alone.
 */

#ifndef NTCOMPAT_SIMD_POLICY_H
#define NTCOMPAT_SIMD_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Instruction sets, as bits of ntcompat_simd.allowed and .cpu. */
#define NTCOMPAT_SIMD_MMX     0x0001u
#define NTCOMPAT_SIMD_SSE     0x0002u
#define NTCOMPAT_SIMD_SSE2    0x0004u
#define NTCOMPAT_SIMD_SSE3    0x0008u
#define NTCOMPAT_SIMD_SSSE3   0x0010u
#define NTCOMPAT_SIMD_SSE41   0x0020u
#define NTCOMPAT_SIMD_SSE42   0x0040u
#define NTCOMPAT_SIMD_AESNI   0x0080u
#define NTCOMPAT_SIMD_PCLMUL  0x0100u
#define NTCOMPAT_SIMD_AVX     0x0200u
#define NTCOMPAT_SIMD_AVX2    0x0400u
#define NTCOMPAT_SIMD_FMA     0x0800u

/* Who saves the registers, as bits of ntcompat_simd.xmm_saved_by etc. */
#define NTCOMPAT_SAVED_BY_NONE    0
#define NTCOMPAT_SAVED_BY_OS      1
#define NTCOMPAT_SAVED_BY_WINSIMD 2

/* WINSIMD 1.2 GuardFlags bits (its include/ctxguard.h). */
#define NTCOMPAT_WINSIMD_ACTIVE 0x01u
#define NTCOMPAT_WINSIMD_XMM    0x02u
#define NTCOMPAT_WINSIMD_YMM    0x04u

typedef struct ntcompat_simd {
    unsigned allowed;        /* NTCOMPAT_SIMD_* this process may use */
    unsigned cpu;            /* NTCOMPAT_SIMD_* the CPU reports */
    unsigned os_major;       /* Windows version the policy was computed for */
    unsigned os_minor;
    int emulated;            /* the version came from NTCOMPAT_EMULATE */
    int sse_probe;           /* 1 = an SSE instruction ran, 0 = it faulted, -1 = not tried */
    int winsimd_present;     /* WINSIMD's Runtime key exists */
    unsigned winsimd_flags;  /* its GuardFlags value, 0 if absent */
    int xmm_saved_by;        /* NTCOMPAT_SAVED_BY_* */
    int ymm_saved_by;
    const char *reason;      /* one-line explanation for logs and about: pages */
} ntcompat_simd;

/*
 * Returns the policy, computed on first use. Safe to call from any thread;
 * concurrent first calls compute the same result.
 *
 * Environment overrides, for testing fallback paths on newer Windows:
 *   NTCOMPAT_EMULATE=nt351|nt4|w2k|xp  compute the policy as on that system
 *                                      (the SSE probe and WINSIMD are still real)
 *   NTCOMPAT_SIMD=off                  allow no SIMD at all
 */
const ntcompat_simd *ntcompat_simd_policy(void);

/* Shorthand: nonzero if every set in *mask* is allowed. */
int ntcompat_simd_allowed(unsigned mask);

#ifdef __cplusplus
}
#endif

#endif /* NTCOMPAT_SIMD_POLICY_H */
