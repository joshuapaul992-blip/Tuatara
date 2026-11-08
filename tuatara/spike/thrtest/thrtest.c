/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * thrtest: find out how thread creation behaves on this system.
 *
 *     THRTEST            run every experiment, each in its own child process
 *     THRTEST /exp N     run experiment N in this process (used by the above)
 *
 * Each experiment runs in a child process so that one that kills its
 * process still yields an exit code (an NTSTATUS such as C0000005 for an
 * access violation). No C runtime; only kernel32 and user32 functions
 * present on Windows NT 3.1. Results also go to THRTEST.TXT.
 */

#include <windows.h>

#ifdef _MSC_VER
#pragma comment(linker, "/ENTRY:entry")
#endif

/* No C runtime: the compiler may still emit memset for structure clearing. */
#ifdef _MSC_VER
#pragma function(memset)
#endif
void *__cdecl memset(void *dst, int c, size_t n)
{
    volatile unsigned char *p = (volatile unsigned char *)dst;
    while (n--)
        *p++ = (unsigned char)c;
    return dst;
}

static HANDLE g_out = INVALID_HANDLE_VALUE, g_log = INVALID_HANDLE_VALUE;

static void say(const char *s)
{
    DWORD n;
    if (g_out != INVALID_HANDLE_VALUE && g_out)
        WriteFile(g_out, s, lstrlenA(s), &n, NULL);
    if (g_log != INVALID_HANDLE_VALUE)
        WriteFile(g_log, s, lstrlenA(s), &n, NULL);
}

/* ---- experiments (run in the child) ---- */

static CRITICAL_SECTION g_cs;
static volatile LONG g_count;

static DWORD WINAPI t_return(LPVOID arg) { return (DWORD)(UINT_PTR)arg; }

static DWORD WINAPI t_console(LPVOID arg)
{
    DWORD n;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "[thread wrote this] ", 20, &n, NULL);
    return (DWORD)(UINT_PTR)arg;
}

static DWORD WINAPI t_user32(LPVOID arg)
{
    char buf[32];
    wsprintfA(buf, "%d", 51);  /* the thread's first user32 call */
    return buf[0] == '5' ? (DWORD)(UINT_PTR)arg : 99;
}

static DWORD WINAPI t_count(LPVOID arg)
{
    int i;
    for (i = 0; i < 100000; i++) {
        EnterCriticalSection(&g_cs);
        g_count++;
        LeaveCriticalSection(&g_cs);
    }
    return (DWORD)(UINT_PTR)arg;
}

/* Starts one thread, waits for it, and checks its exit code is 0x51. */
static DWORD one_thread(LPTHREAD_START_ROUTINE fn, DWORD stack, DWORD flags, BOOL want_id)
{
    DWORD id = 0xDEADBEEF, code = 0;
    HANDLE h = CreateThread(NULL, stack, fn, (LPVOID)0x51, flags, want_id ? &id : NULL);
    if (!h)
        return 0x100 + (GetLastError() & 0xFF);  /* CreateThread failed */
    if (flags & CREATE_SUSPENDED)
        ResumeThread(h);
    if (WaitForSingleObject(h, 15000) != WAIT_OBJECT_0)
        return 0x200;  /* the thread never finished */
    GetExitCodeThread(h, &code);
    CloseHandle(h);
    if (want_id && id == 0xDEADBEEF)
        return 0x300;  /* no thread ID was written */
    return code == 0x51 ? 0 : 0x400 + (code & 0xFF);
}

static DWORD four_threads(void)
{
    HANDLE h[4];
    DWORD id, i;
    InitializeCriticalSection(&g_cs);
    for (i = 0; i < 4; i++) {
        h[i] = CreateThread(NULL, 0, t_count, (LPVOID)0x51, 0, &id);
        if (!h[i])
            return 0x100 + (GetLastError() & 0xFF);
    }
    if (WaitForMultipleObjects(4, h, TRUE, 30000) != WAIT_OBJECT_0)
        return 0x200;
    for (i = 0; i < 4; i++)
        CloseHandle(h[i]);
    return g_count == 400000 ? 0 : 0x500;
}

static const char *const kNames[] = {
    "no thread at all (checks the harness)",
    "CreateThread, thread-ID pointer given",
    "CreateThread, thread-ID pointer NULL",
    "suspended, then ResumeThread",
    "explicit 64 KB stack",
    "the new thread writes to the console",
    "the new thread calls user32 (wsprintfA)",
    "4 threads with a critical section",
};
#define N_EXP (sizeof(kNames) / sizeof(kNames[0]))

static DWORD run_experiment(int n)
{
    switch (n) {
    case 0: return 0;
    case 1: return one_thread(t_return, 0, 0, TRUE);
    case 2: return one_thread(t_return, 0, 0, FALSE);
    case 3: return one_thread(t_return, 0, CREATE_SUSPENDED, TRUE);
    case 4: return one_thread(t_return, 65536, 0, TRUE);
    case 5: return one_thread(t_console, 0, 0, TRUE);
    case 6: return one_thread(t_user32, 0, 0, TRUE);
    case 7: return four_threads();
    }
    return 0xFFFF;
}

/* ---- the supervisor ---- */

static const char *meaning(DWORD code)
{
    switch (code) {
    case 0:          return "ok";
    case 0x200:      return "thread never finished";
    case 0x300:      return "thread ID not written";
    case 0x500:      return "wrong count: threads lost updates";
    case 0xC0000005: return "access violation";
    case 0xC000001D: return "illegal instruction";
    case 0xC00000FD: return "stack overflow";
    case 0xC0000135: return "DLL not found";
    case 0xC0000139: return "entry point not found";
    case 0xC0000142: return "DLL initialisation failed";
    case 0xC0000409: return "fast-fail / stack buffer overrun";
    case 0x00000102: return "hung (killed by the supervisor)";
    }
    if (code >= 0x100 && code < 0x200) return "CreateThread failed (low byte: error code)";
    if (code >= 0x400 && code < 0x500) return "thread returned an unexpected code";
    return "unexpected";
}

static void supervise(void)
{
    char self[MAX_PATH], cmd[MAX_PATH + 32], line[256];
    OSVERSIONINFOA v;
    int n, failures = 0;
    GetModuleFileNameA(NULL, self, sizeof(self));
    v.dwOSVersionInfoSize = sizeof(v);
    if (GetVersionExA(&v)) {
        wsprintfA(line, "thrtest on Windows %lu.%lu build %lu %s\r\n", v.dwMajorVersion,
                  v.dwMinorVersion, v.dwBuildNumber & 0xFFFF, v.szCSDVersion);
        say(line);
    }
    for (n = 0; n < (int)N_EXP; n++) {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        DWORD code = 0xFFFFFFFF;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        wsprintfA(line, "exp %d  %-42s ", n, kNames[n]);
        say(line);
        wsprintfA(cmd, "\"%s\" /exp %d", self, n);
        if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            wsprintfA(line, "could not start (error %lu)\r\n", GetLastError());
            say(line);
            failures++;
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 30000) != WAIT_OBJECT_0)
            TerminateProcess(pi.hProcess, 0x102);
        WaitForSingleObject(pi.hProcess, 5000);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        wsprintfA(line, "exit 0x%08lX  %s\r\n", code, meaning(code));
        say(line);
        if (code)
            failures++;
    }
    wsprintfA(line, "%d of %d experiments failed\r\n", failures, (int)N_EXP);
    say(line);
}

void __cdecl entry(void)
{
    const char *p = GetCommandLineA();
    /* Skip the program name, then look for "/exp N". */
    if (*p == '"') { for (p++; *p && *p != '"'; p++) {} if (*p) p++; }
    else { while (*p && *p != ' ' && *p != '\t') p++; }
    while (*p == ' ' || *p == '\t') p++;
    if ((p[0] == '/' || p[0] == '-') && (p[1] | 0x20) == 'e' && (p[2] | 0x20) == 'x' &&
        (p[3] | 0x20) == 'p') {
        int n = 0;
        for (p += 4; *p == ' '; p++) {}
        while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
        ExitProcess(run_experiment(n));
    }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    g_log = CreateFileA("THRTEST.TXT", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    supervise();
    if (g_log != INVALID_HANDLE_VALUE)
        CloseHandle(g_log);
    ExitProcess(0);
}
