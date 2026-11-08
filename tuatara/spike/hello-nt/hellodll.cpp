/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* HELLODLL.DLL: loaded by hello-nt with LoadLibrary. Checks that a DLL's
 * global constructors and function-local statics work when the DLL is
 * loaded at runtime, the case where implicit TLS breaks before Vista. */

#include <windows.h>
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
