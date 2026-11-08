/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Lazy-bound imports (set "lazy" in thunks.tsv). imp_lazy_x86.asm points
 * each __imp_ slot at a binder; the binder calls ntcompat_lazy_resolve on
 * the first call, which finds the function in the listed DLLs (in order),
 * stores it in the slot and returns it. If no DLL has it, the slot gets a
 * failure stub that calls ntcompat_lazy_failed: set the row's error code,
 * return its failure value.
 *
 * Only functions present on Windows NT 3.1 are called here.
 */

#include "thunks.h"
#include "../core/osversion.h"

#include <windows.h>

typedef struct {
    const char *name;
    const char *dlls;  /* "ws2_32.dll@4.0|wsock32.dll@3.10" */
    DWORD fail;
    DWORD error;
} lazy_info;

static const lazy_info kLazy[] = {
#define NTCOMPAT_LAZY(fn, dlls, fail, err) {#fn, dlls, fail, err},
#include "lazy_table.h"
#undef NTCOMPAT_LAZY
    {NULL, NULL, 0, 0}
};
#define LAZY_COUNT (sizeof(kLazy) / sizeof(kLazy[0]) - 1)

/* Defined in imp_lazy_x86.asm, in table order. */
extern void *volatile ntcompat_lazy_slots[];
extern void *const ntcompat_lazy_binders[];
extern void *const ntcompat_lazy_fail_stubs[];

static unsigned parse_uint(const char **p)
{
    unsigned v = 0;
    while (**p >= '0' && **p <= '9')
        v = v * 10 + (unsigned)(*(*p)++ - '0');
    return v;
}

/* Look *name* up in each "dll@maj.min" of *list* the (effective) system has. */
static FARPROC find(const char *list, const char *name)
{
    const char *p = list;
    while (*p) {
        char dll[64];
        unsigned n = 0, major = 0, minor = 0;
        while (*p && *p != '@' && *p != '|' && n < sizeof(dll) - 1)
            dll[n++] = *p++;
        dll[n] = 0;
        if (*p == '@') {
            p++;
            major = parse_uint(&p);
            if (*p == '.') {
                p++;
                minor = parse_uint(&p);
            }
        }
        if (*p == '|')
            p++;
        /* Under NTCOMPAT_EMULATE, skip DLLs the emulated system wouldn't have. */
        if (ntcompat_os()->emulated && !ntcompat_os_at_least(major, minor))
            continue;
        {
            HMODULE m = GetModuleHandleA(dll);
            if (!m)
                m = LoadLibraryA(dll);
            if (m) {
                FARPROC f = GetProcAddress(m, name);
                if (f)
                    return f;
            }
        }
    }
    return NULL;
}

void *__stdcall ntcompat_lazy_resolve(DWORD index)
{
    void *target;
    DWORD saved = GetLastError();  /* the caller's error state is not ours to change */
    FARPROC f = index < LAZY_COUNT ? find(kLazy[index].dlls, kLazy[index].name) : NULL;
    target = f ? (void *)f : ntcompat_lazy_fail_stubs[index];
    ntcompat_lazy_slots[index] = target;  /* an aligned 32-bit store: atomic */
    SetLastError(saved);
    return target;
}

DWORD __stdcall ntcompat_lazy_failed(DWORD index)
{
    SetLastError(kLazy[index].error);
    return kLazy[index].fail;
}

int ntcompat_lazy_status(const char *function)
{
    unsigned i;
    for (i = 0; i < LAZY_COUNT; i++) {
        if (lstrcmpA(kLazy[i].name, function) == 0) {
            void *slot = ntcompat_lazy_slots[i];
            if (slot == ntcompat_lazy_binders[i])
                return NTCOMPAT_THUNK_UNUSED;
            return slot == ntcompat_lazy_fail_stubs[i] ? NTCOMPAT_THUNK_FALLBACK
                                                       : NTCOMPAT_THUNK_NATIVE;
        }
    }
    return -2;
}
