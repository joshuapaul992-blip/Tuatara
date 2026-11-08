/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * hello-nt: the M0 toolchain spike. Checks that a binary built by a given
 * toolchain starts and works on Windows NT 3.51, NT 4.0 and Windows 2000.
 *
 *     HELLO-NT [/quick] [/nogui]
 *
 * Writes PASS/FAIL/SKIP/INFO lines to the console and to HELLO-NT.TXT in the
 * current directory. The exit code is the number of failures.
 *
 * Only APIs present on NT 3.1 are imported directly; anything newer is looked
 * up with GetProcAddress. Whatever the C/C++ runtime imports is what the
 * spike measures.
 */

#include <windows.h>

#include <intrin.h>
#include <math.h>
#include <stdarg.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "../../../ntcompat/cpu/simd_policy.h"

#ifndef PF_XMMI_INSTRUCTIONS_AVAILABLE
#define PF_XMMI_INSTRUCTIONS_AVAILABLE 6
#endif

#ifndef HELLO_NT_TOOLCHAIN
#define HELLO_NT_TOOLCHAIN "unknown"
#endif

static FILE *g_log;
static int g_pass, g_fail, g_skip;
static bool g_quick, g_nogui;

static void report(const char *status, const char *name, const char *fmt, ...)
{
    char detail[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(detail, sizeof(detail) - 1, fmt, ap);
    detail[sizeof(detail) - 1] = 0;
    va_end(ap);
    printf("%-4s %-14s %s\n", status, name, detail);
    if (g_log)
        fprintf(g_log, "%s\t%s\t%s\n", status, name, detail);
    if (!strcmp(status, "PASS")) g_pass++;
    else if (!strcmp(status, "FAIL")) g_fail++;
    else if (!strcmp(status, "SKIP")) g_skip++;
}

#define CHECK(name, cond, ...) report((cond) ? "PASS" : "FAIL", name, __VA_ARGS__)

/* ---- system information ---- */

static void test_os()
{
    OSVERSIONINFOA v;
    v.dwOSVersionInfoSize = sizeof(v);
    if (GetVersionExA(&v))
        report("INFO", "os", "Windows %lu.%lu build %lu %s", v.dwMajorVersion,
               v.dwMinorVersion, v.dwBuildNumber & 0xFFFF, v.szCSDVersion);
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    MEMORYSTATUS ms;
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatus(&ms);
    report("INFO", "machine", "%lu CPU(s), %lu MB RAM, page size %lu",
           si.dwNumberOfProcessors, (unsigned long)(ms.dwTotalPhys >> 20), si.dwPageSize);
}

/* ---- C and C++ runtime ---- */

struct Counted {
    static int live;
    std::string name;
    explicit Counted(std::string n) : name(std::move(n)) { live++; }
    ~Counted() { live--; }
};
int Counted::live = 0;

static int g_global_ctor_ran;
static struct GlobalInit { GlobalInit() { g_global_ctor_ran = 42; } } g_global_init;

static int local_static_value()
{
    static std::string s = std::string("local") + "-static";  // needs dynamic init
    return (int)s.size();
}

static void test_crt()
{
    CHECK("global-ctor", g_global_ctor_ran == 42, "global constructors ran before main");
    CHECK("local-static", local_static_value() == 12 && local_static_value() == 12,
          "function-local static initialised once");

    void *big = malloc(16 << 20);
    CHECK("malloc", big != nullptr, "16 MB malloc");
    if (big) {
        memset(big, 0xA5, 16 << 20);
        free(big);
    }

    {
        std::vector<std::unique_ptr<Counted>> v;
        for (int i = 0; i < 1000; i++)
            v.push_back(std::make_unique<Counted>("item" + std::to_string(i)));
        std::map<std::string, int> m;
        for (auto &p : v)
            m[p->name] = (int)p->name.size();
        CHECK("containers", m.size() == 1000 && m["item999"] == 7 && Counted::live == 1000,
              "vector, map, string, unique_ptr");
    }
    CHECK("destructors", Counted::live == 0, "all objects destroyed");

    std::optional<int> o = 5;
    std::variant<int, std::string> var = std::string("nt");
    std::string_view sv = "hello-nt";
    CHECK("cxx17", o.value_or(0) == 5 && std::get<std::string>(var) == "nt" &&
          sv.substr(6) == "nt", "optional, variant, string_view");

    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g %.3f", 0.1, 2.5);
    CHECK("printf-float", !strcmp(buf, "0.10000000000000001 2.500"), "\"%s\"", buf);
}

/* ---- floating point (x87 with /arch:IA32) ---- */

static void test_fpu()
{
    unsigned int cw = _controlfp(0, 0);
    const char *pc = (cw & _MCW_PC) == _PC_53 ? "53-bit" :
                     (cw & _MCW_PC) == _PC_64 ? "64-bit" : "24-bit";
    report("INFO", "fpu-precision", "x87 precision control: %s", pc);

    volatile double a = 0.1, b = 0.2, two = 2.0;
    double sum = a + b;
    double root = sqrt(two);
    CHECK("ieee-double", sum == 0.30000000000000004 && root == 1.4142135623730951,
          "0.1+0.2 and sqrt(2) round as IEEE doubles");

    volatile double one = 1.0, half_ulp = DBL_EPSILON / 2;
    double r = one + half_ulp;  // a tie: rounds to 1.0 in double precision
    CHECK("double-rounding", r == 1.0, "1 + 2^-53 == 1 (no extended-precision result)");
}

/* ---- threads and synchronisation ---- */

static CRITICAL_SECTION g_cs;
static long g_counter_cs;
static volatile LONG g_counter_atomic;
static DWORD g_tls_index;

static DWORD WINAPI counting_thread(LPVOID arg)
{
    TlsSetValue(g_tls_index, arg);
    for (int i = 0; i < 100000; i++) {
        EnterCriticalSection(&g_cs);
        g_counter_cs++;
        LeaveCriticalSection(&g_cs);
        InterlockedIncrement((LONG *)&g_counter_atomic);
    }
    return TlsGetValue(g_tls_index) == arg ? 0 : 1;
}

static void test_threads()
{
    InitializeCriticalSection(&g_cs);
    g_tls_index = TlsAlloc();
    HANDLE threads[4];
    for (int i = 0; i < 4; i++)
        threads[i] = CreateThread(nullptr, 0, counting_thread, (LPVOID)(INT_PTR)(i + 1), 0, nullptr);
    DWORD w = WaitForMultipleObjects(4, threads, TRUE, 60000);
    bool tls_ok = true;
    for (HANDLE h : threads) {
        DWORD code = 1;
        GetExitCodeThread(h, &code);
        tls_ok = tls_ok && code == 0;
        CloseHandle(h);
    }
    CHECK("threads", w == WAIT_OBJECT_0 && g_counter_cs == 400000 && g_counter_atomic == 400000,
          "4 threads: critical section %ld, interlocked %ld", g_counter_cs, (long)g_counter_atomic);
    CHECK("tls-api", tls_ok, "TlsAlloc/TlsSetValue per thread");
    TlsFree(g_tls_index);
    DeleteCriticalSection(&g_cs);

    HANDLE ev = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    SetEvent(ev);
    CHECK("event", WaitForSingleObject(ev, 0) == WAIT_OBJECT_0, "manual-reset event");
    CloseHandle(ev);

    volatile LONG64 v64 = 0x100000000LL;
    LONG64 old = _InterlockedCompareExchange64(&v64, 0x200000001LL, 0x100000000LL);
    CHECK("cmpxchg8b", old == 0x100000000LL && v64 == 0x200000001LL,
          "64-bit compare-exchange (cmpxchg8b, no kernel32 import)");
}

/* ---- a DLL loaded at runtime ---- */

static void test_dll()
{
    HMODULE h = LoadLibraryA("HELLODLL.DLL");
    if (!h) {
        report("FAIL", "loadlibrary", "HELLODLL.DLL did not load (error %lu)", GetLastError());
        return;
    }
    typedef int(__cdecl * check_fn)(void);
    check_fn fn = (check_fn)GetProcAddress(h, "hellodll_check");
    int r = fn ? fn() : -1;
    CHECK("loadlibrary", r == 0x5050, "DLL statics and constructors work (got 0x%x)", r);
    CHECK("freelibrary", FreeLibrary(h) != 0, "FreeLibrary");
}

/* ---- files ---- */

static void test_files()
{
    char dir[MAX_PATH], path[MAX_PATH];
    GetTempPathA(sizeof(dir), dir);
    snprintf(path, sizeof(path), "%shello-nt long file name test.txt", dir);
    HANDLE f = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        report("SKIP", "files", "can't create a long-named file in %s (FAT without LFN?)", dir);
        return;
    }
    DWORD n = 0;
    BOOL ok = WriteFile(f, "tuatara", 7, &n, nullptr) && n == 7;
    SetFilePointer(f, 0, nullptr, FILE_BEGIN);
    char back[8] = {0};
    ok = ok && ReadFile(f, back, 7, &n, nullptr) && n == 7 && !memcmp(back, "tuatara", 7);
    CloseHandle(f);
    WIN32_FIND_DATAA fd;
    HANDLE find = FindFirstFileA(path, &fd);
    ok = ok && find != INVALID_HANDLE_VALUE;
    if (find != INVALID_HANDLE_VALUE)
        FindClose(find);
    ok = ok && DeleteFileA(path);
    CHECK("files", ok, "create, write, read, find, delete a long file name");
}

/* ---- Winsock: ws2_32 if present (NT 4.0+), else wsock32 (NT 3.51) ---- */

typedef unsigned int sock_t;
struct sockaddr_in4 { short family; unsigned short port; unsigned long addr; char zero[8]; };

static void test_winsock()
{
    HMODULE ws = LoadLibraryA("WS2_32.DLL");
    const char *which = "ws2_32";
    if (!ws) { ws = LoadLibraryA("WSOCK32.DLL"); which = "wsock32"; }
    if (!ws) { report("SKIP", "winsock", "no Winsock DLL"); return; }

    typedef int(WINAPI * startup_t)(WORD, void *);
    typedef sock_t(WINAPI * socket_t)(int, int, int);
    typedef int(WINAPI * bind_t)(sock_t, const void *, int);
    typedef int(WINAPI * listen_t)(sock_t, int);
    typedef int(WINAPI * getsockname_t)(sock_t, void *, int *);
    typedef int(WINAPI * connect_t)(sock_t, const void *, int);
    typedef sock_t(WINAPI * accept_t)(sock_t, void *, int *);
    typedef int(WINAPI * send_t)(sock_t, const char *, int, int);
    typedef int(WINAPI * recv_t)(sock_t, char *, int, int);
    typedef int(WINAPI * close_t)(sock_t);
    auto p_startup = (startup_t)GetProcAddress(ws, "WSAStartup");
    auto p_socket = (socket_t)GetProcAddress(ws, "socket");
    auto p_bind = (bind_t)GetProcAddress(ws, "bind");
    auto p_listen = (listen_t)GetProcAddress(ws, "listen");
    auto p_getsockname = (getsockname_t)GetProcAddress(ws, "getsockname");
    auto p_connect = (connect_t)GetProcAddress(ws, "connect");
    auto p_accept = (accept_t)GetProcAddress(ws, "accept");
    auto p_send = (send_t)GetProcAddress(ws, "send");
    auto p_recv = (recv_t)GetProcAddress(ws, "recv");
    auto p_close = (close_t)GetProcAddress(ws, "closesocket");
    if (!p_startup || !p_socket || !p_bind || !p_listen || !p_getsockname || !p_connect ||
        !p_accept || !p_send || !p_recv || !p_close) {
        report("FAIL", "winsock", "%s lacks a basic function", which);
        return;
    }
    char wsadata[512];
    if (p_startup(which[1] == 's' && which[2] == '2' ? 0x0202 : 0x0101, wsadata) != 0) {
        report("SKIP", "winsock", "WSAStartup failed (TCP/IP not installed?)");
        return;
    }
    const sock_t bad = (sock_t)~0u;
    sockaddr_in4 addr = {2 /* AF_INET */, 0, 0x0100007F /* 127.0.0.1 */, {0}};
    int len = sizeof(addr);
    sock_t l = p_socket(2, 1, 6);  // AF_INET, SOCK_STREAM, IPPROTO_TCP
    bool ok = l != bad && p_bind(l, &addr, sizeof(addr)) == 0 && p_listen(l, 1) == 0 &&
              p_getsockname(l, &addr, &len) == 0;
    sock_t c = ok ? p_socket(2, 1, 6) : bad;
    ok = ok && c != bad && p_connect(c, &addr, sizeof(addr)) == 0;
    sock_t a = ok ? p_accept(l, nullptr, nullptr) : bad;
    char buf[8] = {0};
    ok = ok && a != bad && p_send(c, "ping", 4, 0) == 4 && p_recv(a, buf, 4, 0) == 4 &&
         !memcmp(buf, "ping", 4);
    if (a != bad) p_close(a);
    if (c != bad) p_close(c);
    if (l != bad) p_close(l);
    CHECK("winsock", ok, "%s: TCP loopback connect, send, receive", which);
}

/* ---- SIMD policy and an XMM preservation stress test ---- */

#if defined(_MSC_VER) && defined(_M_IX86)
/* Loads XMM0-6 with a per-thread pattern, then checks them *iters* times
 * against memory using only SSE1. Returns nonzero if any register changed. */
static unsigned xmm_hold(const float *pat, unsigned iters)
{
    unsigned bad = 0;
    __asm {
        mov eax, pat
        movups xmm0, [eax]
        movups xmm1, [eax+16]
        movups xmm2, [eax+32]
        movups xmm3, [eax+48]
        movups xmm4, [eax+64]
        movups xmm5, [eax+80]
        movups xmm6, [eax+96]
        mov ecx, iters
    again:
        movups xmm7, [eax]
        cmpneqps xmm7, xmm0
        movmskps edx, xmm7
        or bad, edx
        movups xmm7, [eax+48]
        cmpneqps xmm7, xmm3
        movmskps edx, xmm7
        or bad, edx
        movups xmm7, [eax+96]
        cmpneqps xmm7, xmm6
        movmskps edx, xmm7
        or bad, edx
        dec ecx
        jnz again
        movups xmm7, [eax+16]
        cmpneqps xmm7, xmm1
        movmskps edx, xmm7
        or bad, edx
        movups xmm7, [eax+32]
        cmpneqps xmm7, xmm2
        movmskps edx, xmm7
        or bad, edx
        movups xmm7, [eax+64]
        cmpneqps xmm7, xmm4
        movmskps edx, xmm7
        or bad, edx
        movups xmm7, [eax+80]
        cmpneqps xmm7, xmm5
        movmskps edx, xmm7
        or bad, edx
    }
    return bad;
}

struct XmmJob { float pattern[28]; unsigned iters; unsigned bad; };

static DWORD WINAPI xmm_thread(LPVOID arg)
{
    XmmJob *job = (XmmJob *)arg;
    job->bad = xmm_hold(job->pattern, job->iters);
    return 0;
}
#endif

static void test_simd()
{
    const ntcompat_simd *p = ntcompat_simd_policy();
    report("INFO", "cpu-simd", "CPU: %s%s%s%s%s%s", (p->cpu & NTCOMPAT_SIMD_MMX) ? "MMX " : "",
           (p->cpu & NTCOMPAT_SIMD_SSE) ? "SSE " : "", (p->cpu & NTCOMPAT_SIMD_SSE2) ? "SSE2 " : "",
           (p->cpu & NTCOMPAT_SIMD_SSE42) ? "SSE4.2 " : "", (p->cpu & NTCOMPAT_SIMD_AVX) ? "AVX " : "",
           (p->cpu & NTCOMPAT_SIMD_AVX2) ? "AVX2" : "");
    report("INFO", "sse-probe", "%s", p->sse_probe == 1 ? "an SSE instruction ran" :
           p->sse_probe == 0 ? "an SSE instruction faulted (OS has not enabled SSE)" : "not tried");
    report("INFO", "winsimd", p->winsimd_present ? "present, GuardFlags 0x%x" : "not installed",
           p->winsimd_flags);
    HMODULE k32 = GetModuleHandleA("KERNEL32.DLL");
    typedef BOOL(WINAPI * ipfp_t)(DWORD);
    ipfp_t ipfp = (ipfp_t)GetProcAddress(k32, "IsProcessorFeaturePresent");
    if (ipfp)
        report("INFO", "os-xmmi", "IsProcessorFeaturePresent(XMMI) = %d",
               ipfp(PF_XMMI_INSTRUCTIONS_AVAILABLE) ? 1 : 0);
    report("INFO", "simd-policy", "allowed 0x%x%s: %s", p->allowed,
           p->emulated ? " (emulated OS)" : "", p->reason);

    // The policy must never allow SSE on NT 3.51/4.0 without WINSIMD's guard.
    bool consistent = !(p->allowed & NTCOMPAT_SIMD_SSE) || p->os_major >= 5 ||
                      p->xmm_saved_by == NTCOMPAT_SAVED_BY_WINSIMD;
    CHECK("simd-policy", consistent, "SSE allowed only where XMM is saved");

#if defined(_MSC_VER) && defined(_M_IX86)
    if (!(p->allowed & NTCOMPAT_SIMD_SSE)) {
        report("SKIP", "xmm-stress", "SSE not allowed by the policy");
        return;
    }
    static XmmJob jobs[4];
    HANDLE threads[4];
    for (int t = 0; t < 4; t++) {
        for (int i = 0; i < 28; i++)
            jobs[t].pattern[i] = (float)(t * 1000 + i + 1);
        jobs[t].iters = g_quick ? 2000000u : 30000000u;
        jobs[t].bad = 0;
        threads[t] = CreateThread(nullptr, 0, xmm_thread, &jobs[t], 0, nullptr);
    }
    WaitForMultipleObjects(4, threads, TRUE, INFINITE);
    unsigned bad = 0;
    for (int t = 0; t < 4; t++) {
        bad |= jobs[t].bad;
        CloseHandle(threads[t]);
    }
    CHECK("xmm-stress", bad == 0, "4 threads kept XMM0-6 across thread switches");
#else
    report("SKIP", "xmm-stress", "needs an MSVC-compatible compiler (inline assembly)");
#endif
}

/* ---- a GDI window ---- */

static UINT32 g_pixels[64 * 64];
static int g_painted_lines;

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = 64;
        bi.bmiHeader.biHeight = -64;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        g_painted_lines = StretchDIBits(dc, 0, 0, 256, 256, 0, 0, 64, 64, g_pixels, &bi,
                                        DIB_RGB_COLORS, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void test_gui()
{
    if (g_nogui) {
        report("SKIP", "gdi-window", "/nogui");
        return;
    }
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++)
            g_pixels[y * 64 + x] = (UINT32)((x * 4) << 16 | (y * 4) << 8 | 0x80);
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hCursor = LoadCursorA(nullptr, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "TuataraHelloNT";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "hello-nt", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 300, 300, nullptr, nullptr,
                                wc.hInstance, nullptr);
    if (!hwnd) {
        report("FAIL", "gdi-window", "CreateWindowEx failed (error %lu)", GetLastError());
        return;
    }
    UpdateWindow(hwnd);
    SetTimer(hwnd, 1, 1500, nullptr);
    MSG m;
    while (GetMessageA(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    CHECK("gdi-window", g_painted_lines == 64, "window shown; StretchDIBits drew %d of 64 lines",
          g_painted_lines);
}

/* ---- timers and memory ---- */

static void test_misc()
{
    LARGE_INTEGER f, a, b;
    bool qpc = QueryPerformanceFrequency(&f) && QueryPerformanceCounter(&a);
    Sleep(50);
    qpc = qpc && QueryPerformanceCounter(&b) && b.QuadPart > a.QuadPart;
    CHECK("qpc", qpc, "QueryPerformanceCounter advances (frequency %lld Hz)",
          (long long)f.QuadPart);

    char *reserve = (char *)VirtualAlloc(nullptr, 256u << 20, MEM_RESERVE, PAGE_NOACCESS);
    bool ok = reserve != nullptr &&
              VirtualAlloc(reserve, 1 << 20, MEM_COMMIT, PAGE_READWRITE) != nullptr;
    if (ok)
        reserve[(1 << 20) - 1] = 1;
    if (reserve)
        VirtualFree(reserve, 0, MEM_RELEASE);
    CHECK("virtualalloc", ok, "reserve 256 MB, commit 1 MB");
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!_stricmp(argv[i], "/quick")) g_quick = true;
        if (!_stricmp(argv[i], "/nogui")) g_nogui = true;
    }
    g_log = fopen("HELLO-NT.TXT", "w");
    report("INFO", "hello-nt", "toolchain %s", HELLO_NT_TOOLCHAIN);
#if defined(__clang__)
    report("INFO", "compiler", "clang %s", __clang_version__);
#elif defined(_MSC_FULL_VER)
    report("INFO", "compiler", "MSVC %d", _MSC_FULL_VER);
#elif defined(__GNUC__)
    report("INFO", "compiler", "gcc %s", __VERSION__);
#endif
    test_os();
    test_crt();
    test_fpu();
    test_threads();
    test_dll();
    test_files();
    test_winsock();
    test_simd();
    test_misc();
    test_gui();
    report("INFO", "summary", "%d passed, %d failed, %d skipped", g_pass, g_fail, g_skip);
    if (g_log)
        fclose(g_log);
    return g_fail;
}
