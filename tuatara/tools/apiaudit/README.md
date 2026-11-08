# apiaudit

`apiaudit` checks that Tuatara's `.exe` and `.dll` files will load on Windows
NT 3.51, NT 4.0 and Windows 2000. For every import it asks: *does this DLL,
and this function in it, exist on that version of Windows?* It also checks the
PE header fields the old loaders care about.

It is pure Python 3 (3.11+ for allowlists) with no dependencies, so it runs in
Linux CI as well as on Windows.

## Commands

All commands run from the repository root.

### Build a baseline from install media

A baseline is the list of exports of each system DLL on one OS. It's made from
your own install media, and only export names and ordinals are committed —
never Microsoft binaries.

1. **Extract the media.** Copy the `I386` folder off the CD (mount the ISO, or
   `7z x`). For a service pack, extract it into a folder without installing
   it: run the service pack executable with `/x` on Windows (NT 4.0 SP6a,
   Windows 2000 SP4). On Linux, `cabextract` or `7z x` can open many of these
   self-extractors; check that the result contains the `.dll`/`.dl_` files.
2. **Expand the DLLs.** Give the base media first, then each service pack;
   files from later folders win. SZDD files (NT 3.51) are expanded built-in;
   cabinet files need `cabextract` installed.

   ```bash
   python3 tuatara/tools/apiaudit media /media/nt4/I386 /media/sp6a --out /tmp/nt4-sp6a-dlls
   ```

3. **Write the baseline.**

   ```bash
   python3 tuatara/tools/apiaudit baseline /tmp/nt4-sp6a-dlls \
       --out tuatara/baselines/nt4-sp6a --os-id nt4-sp6a \
       --name "Windows NT 4.0 SP6a" --version 4.0 --build 1381
   ```

Use these ids so docs and CI agree: `nt351-sp5` (3.51, build 1057),
`nt4-sp6a` (4.0, build 1381), `w2k-sp4` (5.0, build 2195).

### Check binaries

```bash
python3 tuatara/tools/apiaudit check --baselines tuatara/baselines \
    --allowlist tuatara/apiaudit-allowlist.toml path/to/dist
```

Directories are searched recursively. DLLs found among the inputs count as
shipped with the browser, so imports between our own DLLs resolve. Add
`--os nt4-sp6a` to check one OS, `--json findings.json` for machine-readable
output, and `--strict` to fail on warnings too. The exit code is 1 when any
error isn't allowlisted.

### Inspect one file

```bash
python3 tuatara/tools/apiaudit dump path/to/xul.dll
```

## What it checks

Run `python3 tuatara/tools/apiaudit --help` for the full list. The important ones:

| Check | Meaning |
|---|---|
| `missing-function` / `missing-ordinal` | The DLL exists, but not this export |
| `missing-dll` | The DLL doesn't exist on that OS, and we don't ship it |
| `forwarder-broken` | The export exists but forwards to something that doesn't |
| `subsystem-version` | The PE subsystem version is newer than the OS; the loader refuses the file |
| `tls-directory` | A DLL uses implicit TLS, which breaks under `LoadLibrary` before Vista |
| `ordinal-import` | Import by ordinal from a system DLL (fragile across versions) |

Delay-loaded imports are checked too and marked `[delay]`: they don't stop
the program starting, but they crash on first call unless the code checks.

## Allowlist

Some findings are expected: the import is resolved at runtime with a fallback.
Each one goes in a TOML allowlist with a reason:

```toml
[[allow]]
dll = "kernel32.dll"
function = "GetTickCount64"
os = ["nt351-sp5", "nt4-sp6a", "w2k-sp4"]   # optional; default every OS
reason = "Resolved at runtime; falls back to GetTickCount with wrap tracking"
fallback = "ntcompat/kernel32/tickcount.c"   # optional
```

Keys are optional filters (`check`, `dll`, `function`, `file`, `os`, `kind`);
`dll`, `function` and `file` accept `*` wildcards. `reason` is required.
Entries that match nothing are reported so they can be removed.

## Tests

```bash
python3 -m unittest discover -s tuatara/tools/apiaudit/tests -t tuatara/tools
```

The tests build small PE files with `clang`, `lld-link`, `llvm-dlltool` and
`llvm-rc`, and skip those cases when the tools aren't installed.
