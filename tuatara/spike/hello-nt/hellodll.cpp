/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* HELLODLL.DLL: loaded by hello-nt with LoadLibrary. Checks that a DLL's
 * global constructors and function-local statics work when the DLL is
 * loaded at runtime, the case where implicit TLS breaks before Vista. */

#include <windows.h>
#include <intrin.h>
#include <string>

static int g_ctor_value;
static struct Init { Init() { g_ctor_value = 0x50; } } g_init;

static std::string &name()
{
    static std::string s("hello-dll");  // dynamic initialisation on first call
    return s;
}

extern "C" __declspec(dllexport) int __cdecl hellodll_check(void)
{
    return (g_ctor_value << 8) | (name() == "hello-dll" ? 0x50 : 0);
}

/* Implicit TLS in a DLL loaded with LoadLibrary: Windows before Vista
 * doesn't set it up, so it depends on YY-Thunks' DLL entry point. Each
 * thread must see its own copy, starting from the initial value. */
static thread_local int t_value = 7;

static DWORD WINAPI tls_thread(LPVOID)
{
    if (t_value != 7)
        return 2;  /* a new thread didn't get the initial value */
    t_value = 9;
    return t_value == 9 ? 0 : 3;
}

/* The C runtime's TLS index for this DLL; the loader (or YY-Thunks) sets it. */
extern "C" unsigned long _tls_index;

extern "C" __declspec(dllexport) int __cdecl hellodll_tls_check(void)
{
#if defined(_M_IX86)
    /* Check that this thread has a TLS block for us before touching
     * thread_local: without one the access would fault. TEB+0x2C is
     * ThreadLocalStoragePointer on every 32-bit NT. */
    void **blocks = (void **)__readfsdword(0x2C);
    if (!blocks || !blocks[_tls_index])
        return 7;
#endif
    if (t_value != 7)
        return 1;  /* this thread's copy isn't initialised */
    t_value = 8;
    DWORD tid;  /* never NULL: see hello.cpp */
    HANDLE h = CreateThread(nullptr, 0, tls_thread, nullptr, 0, &tid);
    if (!h)
        return 4;
    WaitForSingleObject(h, INFINITE);
    DWORD code = 5;
    GetExitCodeThread(h, &code);
    CloseHandle(h);
    if (code)
        return (int)code;
    return t_value == 8 ? 0 : 6;  /* the other thread changed our copy */
}
