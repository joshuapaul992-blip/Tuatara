/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "simd_policy.h"

#include <windows.h>

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

/* Only functions that exist on Windows NT 3.1 are imported directly. */

static void cpuid(unsigned leaf, unsigned subleaf, unsigned r[4])
{
#if defined(_MSC_VER)
    int regs[4];
    __cpuidex(regs, (int)leaf, (int)subleaf);
    r[0] = (unsigned)regs[0]; r[1] = (unsigned)regs[1];
    r[2] = (unsigned)regs[2]; r[3] = (unsigned)regs[3];
#else
    __cpuid_count(leaf, subleaf, r[0], r[1], r[2], r[3]);
#endif
}

/* XGETBV(0); only call it when CPUID reports OSXSAVE. */
static unsigned xcr0_low(void)
{
    unsigned lo;
#if defined(_MSC_VER) && defined(_M_IX86)
    __asm {
        xor ecx, ecx
        _emit 0x0f
        _emit 0x01
        _emit 0xd0
        mov lo, eax
    }
#elif defined(_MSC_VER)
    lo = (unsigned)_xgetbv(0);
#else
    unsigned hi;
    __asm__ volatile(".byte 0x0f, 0x01, 0xd0" : "=a"(lo), "=d"(hi) : "c"(0));
    (void)hi;
#endif
    return lo;
}

/* Execute one SSE instruction. 1 = it ran, 0 = it faulted, -1 = can't test. */
static int probe_sse(void)
{
#if defined(_MSC_VER) && defined(_M_IX86)
    __try {
        __asm xorps xmm7, xmm7
    } __except (GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION
                    ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return 0;
    }
    return 1;
#else
    /* GCC has no __try. These builds are only for smoke tests on new
     * Windows or Wine; the policy then relies on the OS version. */
    return -1;
#endif
}

static unsigned read_winsimd_flags(int *present)
{
    HKEY key;
    DWORD type = 0, value = 0, size = sizeof(value);
    *present = 0;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SYSTEM\\CurrentControlSet\\Services\\winsimd\\Runtime",
                      0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return 0;
    *present = 1;
    if (RegQueryValueExA(key, "GuardFlags", NULL, &type, (BYTE *)&value, &size)
            != ERROR_SUCCESS || type != REG_DWORD || size != sizeof(value))
        value = 0;  /* unrecognised: treat as no protection */
    RegCloseKey(key);
    return (unsigned)value;
}

static int env_equals(const char *name, const char *want)
{
    char buf[16];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    return n > 0 && n < sizeof(buf) && lstrcmpiA(buf, want) == 0;
}

static unsigned cpu_features(void)
{
    unsigned r[4], max_leaf, f = 0, ecx, edx;
    cpuid(0, 0, r);
    max_leaf = r[0];
    if (max_leaf < 1)
        return 0;
    cpuid(1, 0, r);
    ecx = r[2];
    edx = r[3];
    if (edx & (1u << 23)) f |= NTCOMPAT_SIMD_MMX;
    if (edx & (1u << 25)) f |= NTCOMPAT_SIMD_SSE;
    if (edx & (1u << 26)) f |= NTCOMPAT_SIMD_SSE2;
    if (ecx & (1u << 0))  f |= NTCOMPAT_SIMD_SSE3;
    if (ecx & (1u << 1))  f |= NTCOMPAT_SIMD_PCLMUL;
    if (ecx & (1u << 9))  f |= NTCOMPAT_SIMD_SSSE3;
    if (ecx & (1u << 12)) f |= NTCOMPAT_SIMD_FMA;
    if (ecx & (1u << 19)) f |= NTCOMPAT_SIMD_SSE41;
    if (ecx & (1u << 20)) f |= NTCOMPAT_SIMD_SSE42;
    if (ecx & (1u << 25)) f |= NTCOMPAT_SIMD_AESNI;
    if (ecx & (1u << 28)) f |= NTCOMPAT_SIMD_AVX;
    if (max_leaf >= 7) {
        cpuid(7, 0, r);
        if (r[1] & (1u << 5)) f |= NTCOMPAT_SIMD_AVX2;
    }
    return f;
}

/* AVX also needs the OS (or WINSIMD) to have turned on XSAVE with YMM. */
static int ymm_enabled(void)
{
    unsigned r[4];
    cpuid(1, 0, r);
    if (!(r[2] & (1u << 27)))  /* OSXSAVE */
        return 0;
    return (xcr0_low() & 6u) == 6u;  /* XMM and YMM state enabled */
}

#define XMM_SETS (NTCOMPAT_SIMD_SSE | NTCOMPAT_SIMD_SSE2 | NTCOMPAT_SIMD_SSE3 | \
                  NTCOMPAT_SIMD_SSSE3 | NTCOMPAT_SIMD_SSE41 | NTCOMPAT_SIMD_SSE42 | \
                  NTCOMPAT_SIMD_AESNI | NTCOMPAT_SIMD_PCLMUL)
#define YMM_SETS (NTCOMPAT_SIMD_AVX | NTCOMPAT_SIMD_AVX2 | NTCOMPAT_SIMD_FMA)

static void compute(ntcompat_simd *p)
{
    DWORD v = GetVersion();
    int xmm_ok, ymm_ok;

    p->os_major = LOBYTE(LOWORD(v));
    p->os_minor = HIBYTE(LOWORD(v));
    p->emulated = 1;
    if (env_equals("NTCOMPAT_EMULATE", "nt351"))    { p->os_major = 3; p->os_minor = 51; }
    else if (env_equals("NTCOMPAT_EMULATE", "nt4")) { p->os_major = 4; p->os_minor = 0; }
    else if (env_equals("NTCOMPAT_EMULATE", "w2k")) { p->os_major = 5; p->os_minor = 0; }
    else if (env_equals("NTCOMPAT_EMULATE", "xp"))  { p->os_major = 5; p->os_minor = 1; }
    else p->emulated = 0;

    p->cpu = cpu_features();
    p->winsimd_flags = read_winsimd_flags(&p->winsimd_present);
    p->allowed = p->cpu & NTCOMPAT_SIMD_MMX;  /* MMX shares the x87 registers */
    p->xmm_saved_by = NTCOMPAT_SAVED_BY_NONE;
    p->ymm_saved_by = NTCOMPAT_SAVED_BY_NONE;
    p->sse_probe = -1;

    if (env_equals("NTCOMPAT_SIMD", "off")) {
        p->allowed = 0;
        p->reason = "SIMD turned off by NTCOMPAT_SIMD=off";
        return;
    }
    if (!(p->cpu & NTCOMPAT_SIMD_SSE)) {
        p->reason = "the CPU has no SSE";
        return;
    }

    p->sse_probe = probe_sse();
    if (p->os_major >= 5) {
        /* Windows 2000 and later save XMM themselves. */
        xmm_ok = p->sse_probe != 0;
        if (xmm_ok)
            p->xmm_saved_by = NTCOMPAT_SAVED_BY_OS;
    } else {
        /* NT 3.51 / 4.0: only WINSIMD's guard makes XMM safe. A probe that
         * passes without it means something enabled SSE without saving it. */
        xmm_ok = p->sse_probe != 0 &&
                 (p->winsimd_flags & (NTCOMPAT_WINSIMD_ACTIVE | NTCOMPAT_WINSIMD_XMM)) ==
                     (NTCOMPAT_WINSIMD_ACTIVE | NTCOMPAT_WINSIMD_XMM);
        if (xmm_ok)
            p->xmm_saved_by = NTCOMPAT_SAVED_BY_WINSIMD;
    }
    if (!xmm_ok) {
        if (p->sse_probe == 0)
            p->reason = "SSE is not enabled by the OS (no WINSIMD)";
        else if (p->os_major < 5)
            p->reason = "SSE is enabled but nothing saves XMM registers at thread "
                        "switches (WINSIMD 1.2 guard not active)";
        else
            p->reason = "SSE is not usable";
        return;
    }
    p->allowed |= p->cpu & XMM_SETS;

    ymm_ok = 0;
    if ((p->cpu & NTCOMPAT_SIMD_AVX) && ymm_enabled()) {
        if (p->os_major > 6 || (p->os_major == 6 && p->os_minor >= 1)) {
            ymm_ok = 1;  /* Windows 7 SP1+ enables XSAVE only when it saves YMM */
            p->ymm_saved_by = NTCOMPAT_SAVED_BY_OS;
        } else if ((p->winsimd_flags & (NTCOMPAT_WINSIMD_ACTIVE | NTCOMPAT_WINSIMD_YMM)) ==
                   (NTCOMPAT_WINSIMD_ACTIVE | NTCOMPAT_WINSIMD_YMM)) {
            ymm_ok = 1;
            p->ymm_saved_by = NTCOMPAT_SAVED_BY_WINSIMD;
        }
    }
    if (ymm_ok)
        p->allowed |= p->cpu & YMM_SETS;

    if (p->xmm_saved_by == NTCOMPAT_SAVED_BY_WINSIMD)
        p->reason = ymm_ok ? "SSE and AVX allowed: WINSIMD saves XMM and YMM"
                           : "SSE allowed: WINSIMD saves XMM registers";
    else
        p->reason = ymm_ok ? "SSE and AVX allowed" : "SSE allowed; AVX not saved or not present";
}

static ntcompat_simd g_policy;
static volatile LONG g_state;  /* 0 = not computed, 2 = computed */

const ntcompat_simd *ntcompat_simd_policy(void)
{
    if (g_state != 2) {
        /* Concurrent first callers each compute the same answer into a local
         * copy; the last write wins and every copy is identical. */
        ntcompat_simd local;
        ZeroMemory(&local, sizeof(local));
        compute(&local);
        g_policy = local;
#if defined(_MSC_VER)
        _ReadWriteBarrier();
#else
        __sync_synchronize();
#endif
        g_state = 2;
    }
    return &g_policy;
}

int ntcompat_simd_allowed(unsigned mask)
{
    return (ntcompat_simd_policy()->allowed & mask) == mask;
}
