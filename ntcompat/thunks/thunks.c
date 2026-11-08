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
#include "../crypto/entropy.h"

#include <windows.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

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

/* ---- set "noyy": only linked into builds without YY-Thunks ---- */

PVOID WINAPI ntcompat_EncodePointer(PVOID p)
{
    typedef PVOID(WINAPI * fn_t)(PVOID);
    fn_t real = (fn_t)real_fn(T_EncodePointer);
    /* Encode and decode resolve together (both XP SP2), so they always pair. */
    return real ? real(p) : p;
}

PVOID WINAPI ntcompat_DecodePointer(PVOID p)
{
    typedef PVOID(WINAPI * fn_t)(PVOID);
    fn_t real = (fn_t)real_fn(T_DecodePointer);
    return real ? real(p) : p;
}

/* Fibre-local storage falls back to thread-local storage: without fibres the
 * values behave the same. The callback, which FLS runs at thread exit, isn't
 * called, so per-thread data registered that way is not freed. */
DWORD WINAPI ntcompat_FlsAlloc(void *callback)
{
    typedef DWORD(WINAPI * fn_t)(void *);
    fn_t real = (fn_t)real_fn(T_FlsAlloc);
    return real ? real(callback) : TlsAlloc();
}

BOOL WINAPI ntcompat_FlsFree(DWORD index)
{
    typedef BOOL(WINAPI * fn_t)(DWORD);
    fn_t real = (fn_t)real_fn(T_FlsFree);
    return real ? real(index) : TlsFree(index);
}

PVOID WINAPI ntcompat_FlsGetValue(DWORD index)
{
    typedef PVOID(WINAPI * fn_t)(DWORD);
    fn_t real = (fn_t)real_fn(T_FlsGetValue);
    return real ? real(index) : TlsGetValue(index);
}

BOOL WINAPI ntcompat_FlsSetValue(DWORD index, PVOID value)
{
    typedef BOOL(WINAPI * fn_t)(DWORD, PVOID);
    fn_t real = (fn_t)real_fn(T_FlsSetValue);
    return real ? real(index, value) : TlsSetValue(index, value);
}

#define NTC_GMHE_PIN          0x1
#define NTC_GMHE_UNCHANGED    0x2
#define NTC_GMHE_FROM_ADDRESS 0x4

BOOL WINAPI ntcompat_GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE *module)
{
    typedef BOOL(WINAPI * fn_t)(DWORD, LPCWSTR, HMODULE *);
    fn_t real = (fn_t)real_fn(T_GetModuleHandleExW);
    HMODULE m;
    if (real)
        return real(flags, name, module);
    if (!module) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *module = NULL;
    if (flags & NTC_GMHE_FROM_ADDRESS) {
        MEMORY_BASIC_INFORMATION mbi;
        WCHAR check[8];
        if (!VirtualQuery(name, &mbi, sizeof(mbi)) || !mbi.AllocationBase ||
            !GetModuleFileNameW((HMODULE)mbi.AllocationBase, check, 8)) {
            SetLastError(ERROR_MOD_NOT_FOUND);
            return FALSE;
        }
        m = (HMODULE)mbi.AllocationBase;
    } else {
        m = GetModuleHandleW(name);
        if (!m)
            return FALSE;  /* GetModuleHandleW set the error */
    }
    if (!(flags & NTC_GMHE_UNCHANGED)) {
        /* Take a reference, as the real function does. A pinned module just
         * keeps this reference forever. */
        WCHAR path[MAX_PATH];
        if (GetModuleFileNameW(m, path, MAX_PATH))
            LoadLibraryW(path);
    }
    *module = m;
    return TRUE;
}

BOOL WINAPI ntcompat_InitializeCriticalSectionEx(LPCRITICAL_SECTION cs, DWORD spin, DWORD flags)
{
    typedef BOOL(WINAPI * fn_t)(LPCRITICAL_SECTION, DWORD, DWORD);
    fn_t real = (fn_t)real_fn(T_InitializeCriticalSectionEx);
    if (real)
        return real(cs, spin, flags);
    InitializeCriticalSection(cs);
    return TRUE;
}

/* x86 SList header: low 32 bits Next, then 16 bits Depth, 16 bits Sequence.
 * Updated with cmpxchg8b, which the i686 baseline guarantees. */
static LONGLONG slist_cas(volatile LONGLONG *p, LONGLONG with, LONGLONG expect)
{
#if defined(_MSC_VER)
    return _InterlockedCompareExchange64(p, with, expect);
#else
    return __sync_val_compare_and_swap(p, expect, with);
#endif
}

void WINAPI ntcompat_InitializeSListHead(void *header)
{
    typedef void(WINAPI * fn_t)(void *);
    fn_t real = (fn_t)real_fn(T_InitializeSListHead);
    if (real) {
        real(header);
        return;
    }
    *(volatile LONGLONG *)header = 0;
}

void *WINAPI ntcompat_InterlockedFlushSList(void *header)
{
    typedef void *(WINAPI * fn_t)(void *);
    fn_t real = (fn_t)real_fn(T_InterlockedFlushSList);
    volatile LONGLONG *h = (volatile LONGLONG *)header;
    LONGLONG old, now;
    if (real)
        return real(header);
    do {
        old = *h;
        if ((DWORD)old == 0)
            return NULL;
        /* Empty list, depth 0, sequence kept. */
        now = old & (LONGLONG)0xFFFF000000000000ULL;
    } while (slist_cas(h, now, old) != old);
    return (void *)(UINT_PTR)(DWORD)old;
}

HANDLE WINAPI ntcompat_FindFirstFileExW(LPCWSTR name, int level, LPVOID data, int search_op,
                                        LPVOID filter, DWORD flags)
{
    typedef HANDLE(WINAPI * fn_t)(LPCWSTR, int, LPVOID, int, LPVOID, DWORD);
    fn_t real = (fn_t)real_fn(T_FindFirstFileExW);
    if (real)
        return real(name, level, data, search_op, filter, flags);
    /* Both info levels fill a WIN32_FIND_DATAW; directory-only searches are
     * advisory, so a plain search is a correct answer. */
    return FindFirstFileW(name, (LPWIN32_FIND_DATAW)data);
}

/* RtlGenRandom, NSS's random source. */
BOOLEAN WINAPI ntcompat_SystemFunction036(PVOID buffer, ULONG length)
{
    typedef BOOLEAN(WINAPI * fn_t)(PVOID, ULONG);
    fn_t real = (fn_t)real_fn(T_SystemFunction036);
    if (real)
        return real(buffer, length);
    return ntcompat_random(buffer, length) ? TRUE : FALSE;
}

/* ---- needed by NSPR/NSS ---- */

static int has_wildcard(const char *a, const WCHAR *w)
{
    if (a) {
        for (; *a; a++)
            if (*a == '*' || *a == '?')
                return 1;
    } else {
        for (; *w; w++)
            if (*w == L'*' || *w == L'?')
                return 1;
    }
    return 0;
}

static void zero(void *p, size_t n)
{
    volatile unsigned char *b = (volatile unsigned char *)p;
    while (n--)
        *b++ = 0;
}

/* GetFileExInfoStandard (level 0) is the only level. FindFirstFile fills
 * the same fields; drive roots ("C:\") aren't found by it, so those fall
 * back to GetFileAttributes alone. */
BOOL WINAPI ntcompat_GetFileAttributesExA(LPCSTR name, int level, LPVOID info)
{
    typedef BOOL(WINAPI * fn_t)(LPCSTR, int, LPVOID);
    fn_t real = (fn_t)real_fn(T_GetFileAttributesExA);
    WIN32_FILE_ATTRIBUTE_DATA *d = (WIN32_FILE_ATTRIBUTE_DATA *)info;
    WIN32_FIND_DATAA fd;
    HANDLE h;
    if (real)
        return real(name, level, info);
    if (level != 0 || !name || !info) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (has_wildcard(name, NULL)) {
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }
    h = FindFirstFileA(name, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD attrs = GetFileAttributesA(name);
        if (attrs == 0xFFFFFFFF)
            return FALSE;
        zero(d, sizeof(*d));
        d->dwFileAttributes = attrs;
        return TRUE;
    }
    FindClose(h);
    d->dwFileAttributes = fd.dwFileAttributes;
    d->ftCreationTime = fd.ftCreationTime;
    d->ftLastAccessTime = fd.ftLastAccessTime;
    d->ftLastWriteTime = fd.ftLastWriteTime;
    d->nFileSizeHigh = fd.nFileSizeHigh;
    d->nFileSizeLow = fd.nFileSizeLow;
    return TRUE;
}

BOOL WINAPI ntcompat_GetFileAttributesExW(LPCWSTR name, int level, LPVOID info)
{
    typedef BOOL(WINAPI * fn_t)(LPCWSTR, int, LPVOID);
    fn_t real = (fn_t)real_fn(T_GetFileAttributesExW);
    WIN32_FILE_ATTRIBUTE_DATA *d = (WIN32_FILE_ATTRIBUTE_DATA *)info;
    WIN32_FIND_DATAW fd;
    HANDLE h;
    if (real)
        return real(name, level, info);
    if (level != 0 || !name || !info) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (has_wildcard(NULL, name)) {
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }
    h = FindFirstFileW(name, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD attrs = GetFileAttributesW(name);
        if (attrs == 0xFFFFFFFF)
            return FALSE;
        zero(d, sizeof(*d));
        d->dwFileAttributes = attrs;
        return TRUE;
    }
    FindClose(h);
    d->dwFileAttributes = fd.dwFileAttributes;
    d->ftCreationTime = fd.ftCreationTime;
    d->ftLastAccessTime = fd.ftLastAccessTime;
    d->ftLastWriteTime = fd.ftLastWriteTime;
    d->nFileSizeHigh = fd.nFileSizeHigh;
    d->nFileSizeLow = fd.nFileSizeLow;
    return TRUE;
}

/* The fallback works on the critical section's fields with the classic
 * semantics (LockCount -1 = free), which NT 3.51 has and Windows Server 2003
 * SP1 and later don't. So it must never run on a system that has the real
 * function, not even under NTCOMPAT_EMULATE. */
BOOL WINAPI ntcompat_TryEnterCriticalSection(LPCRITICAL_SECTION cs)
{
    typedef BOOL(WINAPI * fn_t)(LPCRITICAL_SECTION);
    static fn_t real;
    static int looked;
    HANDLE me;
    if (!looked) {
        real = (fn_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "TryEnterCriticalSection");
        looked = 1;
    }
    if (real) {
        g_real[T_TryEnterCriticalSection] = (FARPROC)real;  /* for ntcompat_thunk_status */
        return real(cs);
    }
    g_real[T_TryEnterCriticalSection] = NULL;
    me = (HANDLE)(UINT_PTR)GetCurrentThreadId();
#if defined(_MSC_VER)
    if (_InterlockedCompareExchange(&cs->LockCount, 0, -1) == -1) {
#else
    if (__sync_val_compare_and_swap(&cs->LockCount, -1, 0) == -1) {
#endif
        cs->OwningThread = me;
        cs->RecursionCount = 1;
        return TRUE;
    }
    if (cs->OwningThread == me) {
        InterlockedIncrement(&cs->LockCount);
        cs->RecursionCount++;
        return TRUE;
    }
    return FALSE;
}

BOOL WINAPI ntcompat_GlobalMemoryStatusEx(LPMEMORYSTATUSEX m)
{
    typedef BOOL(WINAPI * fn_t)(LPMEMORYSTATUSEX);
    fn_t real = (fn_t)real_fn(T_GlobalMemoryStatusEx);
    MEMORYSTATUS s;
    if (real)
        return real(m);
    if (!m || m->dwLength != sizeof(*m)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    s.dwLength = sizeof(s);
    GlobalMemoryStatus(&s);
    m->dwMemoryLoad = s.dwMemoryLoad;
    m->ullTotalPhys = s.dwTotalPhys;
    m->ullAvailPhys = s.dwAvailPhys;
    m->ullTotalPageFile = s.dwTotalPageFile;
    m->ullAvailPageFile = s.dwAvailPageFile;
    m->ullTotalVirtual = s.dwTotalVirtual;
    m->ullAvailVirtual = s.dwAvailVirtual;
    m->ullAvailExtendedVirtual = 0;
    return TRUE;
}

/* Fallback: the time zone's standard bias only (no daylight-saving rules),
 * or for the current zone the system's current bias. */
BOOL WINAPI ntcompat_SystemTimeToTzSpecificLocalTime(LPTIME_ZONE_INFORMATION tz,
                                                     LPSYSTEMTIME utc, LPSYSTEMTIME local)
{
    typedef BOOL(WINAPI * fn_t)(LPTIME_ZONE_INFORMATION, LPSYSTEMTIME, LPSYSTEMTIME);
    fn_t real = (fn_t)real_fn(T_SystemTimeToTzSpecificLocalTime);
    FILETIME ft, lft;
    if (real)
        return real(tz, utc, local);
    if (!SystemTimeToFileTime(utc, &ft))
        return FALSE;
    if (tz) {
        ULARGE_INTEGER t;
        LONGLONG bias = (LONGLONG)(tz->Bias + tz->StandardBias) * 60 * 10000000;
        t.LowPart = ft.dwLowDateTime;
        t.HighPart = ft.dwHighDateTime;
        t.QuadPart -= (ULONGLONG)bias;  /* local = UTC - bias */
        lft.dwLowDateTime = t.LowPart;
        lft.dwHighDateTime = t.HighPart;
    } else if (!FileTimeToLocalFileTime(&ft, &lft)) {
        return FALSE;
    }
    return FileTimeToSystemTime(&lft, local);
}
