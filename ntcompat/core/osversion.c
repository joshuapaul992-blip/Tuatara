/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "osversion.h"

#include <windows.h>

static ntcompat_osversion g_os;
static volatile LONG g_done;

static int env_is(const char *want)
{
    char buf[16];
    DWORD n = GetEnvironmentVariableA("NTCOMPAT_EMULATE", buf, sizeof(buf));
    return n > 0 && n < sizeof(buf) && lstrcmpiA(buf, want) == 0;
}

const ntcompat_osversion *ntcompat_os(void)
{
    if (g_done != 1) {
        /* Racing first callers compute identical values. */
        ntcompat_osversion v;
        DWORD raw = GetVersion();
        v.major = LOBYTE(LOWORD(raw));
        v.minor = HIBYTE(LOWORD(raw));
        v.emulated = 1;
        if (env_is("nt351"))    { v.major = 3; v.minor = 51; }
        else if (env_is("nt4")) { v.major = 4; v.minor = 0; }
        else if (env_is("w2k")) { v.major = 5; v.minor = 0; }
        else if (env_is("xp"))  { v.major = 5; v.minor = 1; }
        else v.emulated = 0;
        g_os = v;
        g_done = 1;
    }
    return &g_os;
}

int ntcompat_os_at_least(unsigned major, unsigned minor)
{
    const ntcompat_osversion *v = ntcompat_os();
    return v->major > major || (v->major == major && v->minor >= minor);
}
