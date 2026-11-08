/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Stubs behind the __imp_ pointers in imp_x86.asm (generated from
 * thunks.tsv). Each looks the real function up once; if the system lacks it
 * (or NTCOMPAT_EMULATE says the emulated system would), it runs a fallback.
 *
 * This file must not call any thunked function through its import: that
 * would loop back into the stub. It uses only functions present on NT 3.1.
 */

#include "thunks.h"
#include "../core/osversion.h"
#include "../cpu/simd_policy.h"

#include <windows.h>

/* ---- the table ---- */

enum {
#define NTCOMPAT_THUNK(dll, fn, maj, min) T_##fn,
#include "thunk_table.h"
#undef NTCOMPAT_THUNK
    T_COUNT
};

typedef struct {
    const char *dll;
    const char *name;
    unsigned since_major, since_minor;
} thunk_info;

static const thunk_info kThunks[T_COUNT] = {
#define NTCOMPAT_THUNK(dll, fn, maj, min) {dll, #fn, maj, min},
#include "thunk_table.h"
#undef NTCOMPAT_THUNK
};

#define UNRESOLVED ((FARPROC)(INT_PTR)1)
static FARPROC volatile g_real[T_COUNT] = {
#define NTCOMPAT_THUNK(dll, fn, maj, min) UNRESOLVED,
#include "thunk_table.h"
#undef NTCOMPAT_THUNK
};

/* The system's function, or NULL to use the fallback. Racing first calls
 * all store the same answer. */
static FARPROC real_fn(int i)
{
    FARPROC p = g_real[i];
    if (p == UNRESOLVED) {
        const thunk_info *t = &kThunks[i];
        p = NULL;
        if (!ntcompat_os()->emulated ||
            ntcompat_os_at_least(t->since_major, t->since_minor)) {
            HMODULE m = GetModuleHandleA(t->dll);
            if (!m && lstrcmpiA(t->dll, "kernel32.dll") != 0)
                m = LoadLibraryA(t->dll);  /* e.g. ole32, loaded on demand */
            if (m)
                p = GetProcAddress(m, t->name);
        }
        g_real[i] = p;
    }
    return p;
}

int ntcompat_thunk_status(const char *function)
{
    int i;
    for (i = 0; i < T_COUNT; i++) {
        if (lstrcmpA(kThunks[i].name, function) == 0) {
            FARPROC p = g_real[i];
            return p == UNRESOLVED ? NTCOMPAT_THUNK_UNUSED
                                   : p ? NTCOMPAT_THUNK_NATIVE : NTCOMPAT_THUNK_FALLBACK;
        }
    }
    return -2;
}

/* ---- kernel32 ---- */

BOOL WINAPI ntcompat_GetFileSizeEx(HANDLE file, PLARGE_INTEGER size)
{
    typedef BOOL(WINAPI * fn_t)(HANDLE, PLARGE_INTEGER);
    fn_t real = (fn_t)real_fn(T_GetFileSizeEx);
    DWORD low, high = 0;
    if (real)
        return real(file, size);
    SetLastError(NO_ERROR);
    low = GetFileSize(file, &high);
    if (low == 0xFFFFFFFF && GetLastError() != NO_ERROR)
        return FALSE;
    size->LowPart = low;
    size->HighPart = (LONG)high;
    return TRUE;
}

BOOL WINAPI ntcompat_SetFilePointerEx(HANDLE file, LARGE_INTEGER distance,
                                      PLARGE_INTEGER new_position, DWORD method)
{
    typedef BOOL(WINAPI * fn_t)(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);
    fn_t real = (fn_t)real_fn(T_SetFilePointerEx);
    LONG high = distance.HighPart;
    DWORD low;
    if (real)
        return real(file, distance, new_position, method);
    SetLastError(NO_ERROR);
    low = SetFilePointer(file, (LONG)distance.LowPart, &high, method);
    if (low == 0xFFFFFFFF && GetLastError() != NO_ERROR)
        return FALSE;
    if (new_position) {
        new_position->LowPart = low;
        new_position->HighPart = high;
    }
    return TRUE;
}

BOOL WINAPI ntcompat_InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD spin)
{
    typedef BOOL(WINAPI * fn_t)(LPCRITICAL_SECTION, DWORD);
    fn_t real = (fn_t)real_fn(T_InitializeCriticalSectionAndSpinCount);
    if (real)
        return real(cs, spin);
    InitializeCriticalSection(cs);  /* no spinning before NT 4.0 SP3 */
    return TRUE;
}

BOOL WINAPI ntcompat_IsDebuggerPresent(void)
{
    typedef BOOL(WINAPI * fn_t)(void);
    fn_t real = (fn_t)real_fn(T_IsDebuggerPresent);
    return real ? real() : FALSE;
}

BOOL WINAPI ntcompat_AreFileApisANSI(void)
{
    typedef BOOL(WINAPI * fn_t)(void);
    fn_t real = (fn_t)real_fn(T_AreFileApisANSI);
    return real ? real() : TRUE;  /* ANSI is the default and the only mode we set */
}

/* Processor feature numbers (winnt.h); listed here so older SDKs work too. */
enum {
    PF_FP_ERRATA = 0, PF_FP_EMULATED = 1, PF_CMPXCHG8B = 2, PF_MMX = 3,
    PF_XMMI = 6, PF_3DNOW = 7, PF_RDTSC = 8, PF_XMMI64 = 10, PF_SSE3 = 13,
    PF_XSAVE = 17, PF_SSSE3 = 36, PF_SSE41 = 37, PF_SSE42 = 38, PF_AVX = 39,
    PF_AVX2 = 40, PF_AVX512F = 41
};

BOOL WINAPI ntcompat_IsProcessorFeaturePresent(DWORD feature)
{
    typedef BOOL(WINAPI * fn_t)(DWORD);
    fn_t real;
    /* SIMD answers follow the policy everywhere, so the C runtime's own
     * SSE2 dispatch agrees with ours (decision 0009). */
    switch (feature) {
    case PF_XMMI:     return ntcompat_simd_allowed(NTCOMPAT_SIMD_SSE);
    case PF_XMMI64:   return ntcompat_simd_allowed(NTCOMPAT_SIMD_SSE2);
    case PF_SSE3:     return ntcompat_simd_allowed(NTCOMPAT_SIMD_SSE3);
    case PF_SSSE3:    return ntcompat_simd_allowed(NTCOMPAT_SIMD_SSSE3);
    case PF_SSE41:    return ntcompat_simd_allowed(NTCOMPAT_SIMD_SSE41);
    case PF_SSE42:    return ntcompat_simd_allowed(NTCOMPAT_SIMD_SSE42);
    case PF_XSAVE:
    case PF_AVX:      return ntcompat_simd_allowed(NTCOMPAT_SIMD_AVX);
    case PF_AVX2:     return ntcompat_simd_allowed(NTCOMPAT_SIMD_AVX2);
    case PF_AVX512F:  return FALSE;  /* never used */
    }
    real = (fn_t)real_fn(T_IsProcessorFeaturePresent);
    if (real)
        return real(feature);
    switch (feature) {
    case PF_CMPXCHG8B:
    case PF_RDTSC:    return TRUE;  /* guaranteed by the i686 baseline */
    case PF_MMX:      return ntcompat_simd_allowed(NTCOMPAT_SIMD_MMX);
    default:          return FALSE; /* includes fast-fail: not on old systems */
    }
}

/* ---- ole32 ---- */

LPVOID WINAPI ntcompat_CoTaskMemAlloc(SIZE_T size)
{
    typedef LPVOID(WINAPI * fn_t)(SIZE_T);
    fn_t real = (fn_t)real_fn(T_CoTaskMemAlloc);
    /* Without OLE, nothing can call CoTaskMemFree on this memory either. */
    return real ? real(size) : HeapAlloc(GetProcessHeap(), 0, size);
}
