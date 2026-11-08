/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * ntexports: write the export tables of this machine's system DLLs to a
 * text file, for apiaudit baselines.
 *
 *     NTEXPORT [output-file]           (default: EXPORTS.TXT)
 *
 * It is built without a C runtime and imports only kernel32 functions that
 * exist on Windows NT 3.1, so it runs on every target system. DLLs are
 * mapped with DONT_RESOLVE_DLL_REFERENCES, so none of their code runs; the
 * tables are read from the mapped image. Only names, ordinals, forwarders
 * and version numbers are written, never DLL contents.
 *
 * Output format (tab-separated, one record per line):
 *     TUATARA-NTEXPORTS  1
 *     os       <major> <minor> <build> <service pack text>
 *     dll      <name> <file version or -> <machine, hex>
 *     e        <ordinal> <name or empty> <forwarder or empty>
 *     missing  <name>
 *     end
 */

#include <windows.h>

#ifdef _MSC_VER
#pragma comment(linker, "/ENTRY:entry")
#endif

static const char *const kDlls[] = {
    "advapi32.dll", "comctl32.dll", "comdlg32.dll", "crtdll.dll", "crypt32.dll",
    "dbghelp.dll", "ddraw.dll", "dnsapi.dll", "dsound.dll", "gdi32.dll",
    "glu32.dll", "imagehlp.dll", "imm32.dll", "iphlpapi.dll", "kernel32.dll",
    "lz32.dll", "mpr.dll", "msacm32.dll", "msimg32.dll", "msvcrt.dll",
    "msvcrt20.dll", "msvcrt40.dll", "mswsock.dll", "netapi32.dll", "ntdll.dll",
    "ole32.dll", "oleacc.dll", "oleaut32.dll", "olecli32.dll", "opengl32.dll",
    "psapi.dll", "rasapi32.dll", "riched20.dll", "riched32.dll", "rpcrt4.dll",
    "secur32.dll", "setupapi.dll", "shell32.dll", "shfolder.dll", "shlwapi.dll",
    "user32.dll", "userenv.dll", "usp10.dll", "uxtheme.dll", "version.dll",
    "wininet.dll", "winmm.dll", "winspool.drv", "wintrust.dll", "ws2_32.dll",
    "wsock32.dll",
};

/* ---- output buffer ---- */

static HANDLE g_out = INVALID_HANDLE_VALUE;
static char g_buf[32768];
static DWORD g_len;
static int g_failed;

static void flush(void)
{
    DWORD written;
    if (g_len && !WriteFile(g_out, g_buf, g_len, &written, NULL))
        g_failed = 1;
    g_len = 0;
}

static void put(const char *s)
{
    while (*s) {
        if (g_len == sizeof(g_buf))
            flush();
        g_buf[g_len++] = *s++;
    }
}

static void put_n(const char *s, DWORD max)
{
    DWORD i;
    for (i = 0; i < max && s[i]; i++) {
        if (g_len == sizeof(g_buf))
            flush();
        /* Keep the format line-safe even for odd strings. */
        g_buf[g_len++] = (s[i] == '\t' || s[i] == '\r' || s[i] == '\n') ? ' ' : s[i];
    }
}

static void put_dec(DWORD v)
{
    char tmp[12];
    int i = 0;
    do {
        tmp[i++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (i)
        put_n(&tmp[--i], 1);
}

static void put_hex(DWORD v)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[9];
    int i = 0;
    do {
        tmp[i++] = digits[v & 15];
        v >>= 4;
    } while (v);
    while (i)
        put_n(&tmp[--i], 1);
}

static void console(const char *s)
{
    DWORD written;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE && h != NULL)
        WriteFile(h, s, lstrlenA(s), &written, NULL);
}

/* ---- image reading (from the mapped module) ---- */

static IMAGE_NT_HEADERS *nt_headers(BYTE *base)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt : NULL;
}

/* First RT_VERSION resource, walked by hand: version.dll may be missing. */
static int file_version(BYTE *base, IMAGE_NT_HEADERS *nt, DWORD *ms, DWORD *ls)
{
    IMAGE_DATA_DIRECTORY *d = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
    IMAGE_RESOURCE_DIRECTORY *root, *dir;
    IMAGE_RESOURCE_DIRECTORY_ENTRY *e;
    IMAGE_RESOURCE_DATA_ENTRY *data;
    DWORD i, n, level;
    BYTE *p, *end;

    if (!d->VirtualAddress || !d->Size)
        return 0;
    root = (IMAGE_RESOURCE_DIRECTORY *)(base + d->VirtualAddress);
    e = (IMAGE_RESOURCE_DIRECTORY_ENTRY *)(root + 1);
    n = root->NumberOfNamedEntries + root->NumberOfIdEntries;
    for (i = 0; i < n; i++, e++) {
        if (!(e->Name & 0x80000000) && e->Name == 16 /* RT_VERSION */ &&
            (e->OffsetToData & 0x80000000))
            break;
    }
    if (i == n)
        return 0;
    dir = (IMAGE_RESOURCE_DIRECTORY *)((BYTE *)root + (e->OffsetToData & 0x7FFFFFFF));
    for (level = 0; level < 2; level++) {
        if (dir->NumberOfNamedEntries + dir->NumberOfIdEntries == 0)
            return 0;
        e = (IMAGE_RESOURCE_DIRECTORY_ENTRY *)(dir + 1);
        if (!(e->OffsetToData & 0x80000000))
            break;
        dir = (IMAGE_RESOURCE_DIRECTORY *)((BYTE *)root + (e->OffsetToData & 0x7FFFFFFF));
    }
    if (e->OffsetToData & 0x80000000)
        return 0;
    data = (IMAGE_RESOURCE_DATA_ENTRY *)((BYTE *)root + e->OffsetToData);
    p = base + data->OffsetToData;
    end = p + data->Size;
    for (; p + 16 <= end; p += 4) {  /* VS_FIXEDFILEINFO is DWORD-aligned */
        if (*(DWORD *)p == 0xFEEF04BD) {
            *ms = ((DWORD *)p)[2];
            *ls = ((DWORD *)p)[3];
            return 1;
        }
    }
    return 0;
}

static void dump_dll(const char *name)
{
    /* DONT_RESOLVE_DLL_REFERENCES: map only, run no DllMain. */
    HMODULE mod = LoadLibraryExA(name, NULL, DONT_RESOLVE_DLL_REFERENCES);
    BYTE *base = (BYTE *)mod;
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY *d;
    IMAGE_EXPORT_DIRECTORY *ex;
    DWORD *funcs, *names;
    WORD *ords;
    DWORD i, j, ms, ls;

    if (!mod) {
        put("missing\t");
        put(name);
        put("\n");
        return;
    }
    nt = nt_headers(base);
    put("dll\t");
    put(name);
    put("\t");
    if (nt && file_version(base, nt, &ms, &ls)) {
        put_dec(ms >> 16); put("."); put_dec(ms & 0xFFFF); put(".");
        put_dec(ls >> 16); put("."); put_dec(ls & 0xFFFF);
    } else {
        put("-");
    }
    put("\t");
    put_hex(nt ? nt->FileHeader.Machine : 0);
    put("\n");

    if (nt) {
        d = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (d->VirtualAddress && d->Size) {
            ex = (IMAGE_EXPORT_DIRECTORY *)(base + d->VirtualAddress);
            funcs = (DWORD *)(base + ex->AddressOfFunctions);
            names = (DWORD *)(base + ex->AddressOfNames);
            ords = (WORD *)(base + ex->AddressOfNameOrdinals);
            for (i = 0; i < ex->NumberOfFunctions; i++) {
                DWORD rva = funcs[i];
                if (!rva)
                    continue;
                put("e\t");
                put_dec(ex->Base + i);
                put("\t");
                for (j = 0; j < ex->NumberOfNames; j++) {
                    if (ords[j] == i) {
                        put_n((const char *)(base + names[j]), 512);
                        break;
                    }
                }
                put("\t");
                if (rva >= d->VirtualAddress && rva < d->VirtualAddress + d->Size)
                    put_n((const char *)(base + rva), 512);
                put("\n");
            }
        }
    }
    FreeLibrary(mod);
}

void __cdecl entry(void)
{
    char *cmd = GetCommandLineA();
    char path[MAX_PATH];
    OSVERSIONINFOA ver;
    DWORD i, n = 0;

    /* Skip the program name (quoted or not), then take the rest as the path. */
    if (*cmd == '"') {
        for (cmd++; *cmd && *cmd != '"'; cmd++) {}
        if (*cmd) cmd++;
    } else {
        while (*cmd && *cmd != ' ' && *cmd != '\t') cmd++;
    }
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    while (cmd[n] && cmd[n] != ' ' && n < MAX_PATH - 1) {
        path[n] = cmd[n];
        n++;
    }
    path[n] = 0;
    if (!n)
        lstrcpyA(path, "EXPORTS.TXT");

    g_out = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_out == INVALID_HANDLE_VALUE) {
        console("ntexports: cannot create the output file\r\n");
        ExitProcess(2);
    }

    /* Errors from LoadLibraryEx should not pop up dialogs. */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    put("TUATARA-NTEXPORTS\t1\n");
    ver.dwOSVersionInfoSize = sizeof(ver);
    if (GetVersionExA(&ver)) {
        put("os\t");
        put_dec(ver.dwMajorVersion); put("\t");
        put_dec(ver.dwMinorVersion); put("\t");
        put_dec(ver.dwBuildNumber & 0xFFFF); put("\t");
        put_n(ver.szCSDVersion, sizeof(ver.szCSDVersion));
        put("\n");
    }
    for (i = 0; i < sizeof(kDlls) / sizeof(kDlls[0]); i++)
        dump_dll(kDlls[i]);
    put("end\n");
    flush();
    CloseHandle(g_out);

    if (g_failed) {
        console("ntexports: writing the output file failed (disk full?)\r\n");
        ExitProcess(1);
    }
    console("ntexports: wrote ");
    console(path);
    console("\r\n");
    ExitProcess(0);
}
