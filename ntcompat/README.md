# ntcompat

Tuatara's compatibility layer for Windows NT 3.51, NT 4.0 and Windows 2000.
It is linked statically into every binary; nothing is installed on the system.

| Part | What it does |
|---|---|
| [`core/osversion`](core/osversion.h) | The Windows version ntcompat decides for: the real one, or the one named by `NTCOMPAT_EMULATE` |
| [`cpu/simd_policy`](cpu/simd_policy.h) | Which SIMD sets may be used: only where the OS or WINSIMD 1.2 saves their registers ([decision 0009](../tuatara/docs/decisions/0009-winsimd.md)) |
| [`thunks/`](thunks/thunks.tsv) | Stand-ins for imports that NT 3.51 and NT 4.0 lack |

## Import thunks

Code that calls a `dllimport` function, such as the C runtime calling
`GetFileSizeEx`, reads a pointer named `__imp__GetFileSizeEx@8`.
`thunks/imp_x86.asm` defines those pointers, plus the direct-call entry points
an import library would have, pointing at ntcompat's stubs in `thunks.c`. The
objects are linked before the system import libraries, so the function never
appears in the PE import table, and the binary loads on systems that lack it.
Each stub looks up the real function once and calls it if the system has it;
otherwise it runs a fallback. This is the technique YY-Thunks uses down to
Windows 2000; ntcompat continues below it.

The table has two sets. **core** (`imp_x86.asm`) is linked into every build:
functions NT 3.51 or NT 4.0 lack that YY-Thunks doesn't cover, because they
exist in Windows 2000. **noyy** (`imp_noyy_x86.asm`) is linked only into
builds without YY-Thunks, and covers what YY-Thunks would (`EncodePointer`,
the `Fls*` functions, `GetModuleHandleExW`, SLists and so on), so a binary can
run on NT 3.51 with ntcompat alone.

To add a function: add a line to [`thunks/thunks.tsv`](thunks/thunks.tsv), write
`ntcompat_<function>` in `thunks.c`, and run:

```bash
python3 ntcompat/thunks/gen.py
```

CI checks that the generated files are current (`gen.py --check`) and that the
spike binaries no longer import any thunked function (`gen.py --check-imports`).

## Testing fallbacks on new Windows

`NTCOMPAT_EMULATE=nt351` (or `nt4`, `w2k`, `xp`) makes ntcompat behave as on
that system: thunked functions newer than it take their fallback, and the SIMD
policy decides as there. It does not change the Windows loader, so whether a
binary *loads* on an old system is still only shown on that system.
