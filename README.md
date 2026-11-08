# Tuatara

**A modern web browser for Windows NT 3.51, NT 4.0 and Windows 2000.**

Tuatara is a Basilisk-style browser (the Firefox 29–56 "Australis" look, with
classic options) built on **Quentix**, our engine. Quentix is based on the
Unified XUL Platform (UXP) by way of [roytam1's tree](https://github.com/roytam1/UXP),
which already keeps UXP running on Windows XP and on CPUs without SSE2.
Tuatara takes it further back, to the Windows versions of the 1990s.

> **Status: pre-alpha, planning and tooling (milestone M0).** There is no
> browser to download yet.

## Supported systems (planned)

| System | Status | JavaScript JIT | WebAssembly |
|---|---|---|---|
| Windows 2000 SP4 | First target | Yes, on CPUs with SSE2 | Yes, with the JIT |
| Windows NT 4.0 SP6a | Second target | With [WINSIMD](https://github.com/ages2001/WINSIMD) 1.2 and an SSE2 CPU | With the JIT |
| Windows NT 3.51 SP5 | Stretch goal | With WINSIMD 1.2 and an SSE2 CPU | With the JIT |
| Windows XP and later | Should work; not the focus | Yes | Yes |

The minimum CPU is a Pentium Pro (i686). SSE and SSE2 are used only when the
CPU has them and something saves their registers at thread switches: the OS
on Windows 2000 and later, or the optional WINSIMD 1.2 driver on NT 4.0 and
NT 3.51. WINSIMD is a separate project and is never required. See
[supported systems](tuatara/docs/supported-systems.md) for the details and limits.

## Layout

Tuatara lives on the `tuatara` branch of this repository, which is a fork of
roytam1/UXP.

| Path | Contents |
|---|---|
| `application/tuatara/` | The browser front end (to come in M3, starting from `application/basilisk`) |
| `ntcompat/` | Fallbacks for Win32 APIs missing on old NT (to come) |
| `tuatara/docs/` | [Plan](tuatara/docs/plan.md), [decisions](tuatara/docs/decisions/), systems matrix |
| `tuatara/tools/apiaudit/` | [Checks binaries](tuatara/tools/apiaudit/README.md) against each OS's real API surface |
| `tuatara/baselines/` | Export lists of system DLLs per OS (names only, no Microsoft binaries) |
| everything else | The UXP engine, as in roytam1's `ia32` branch |

The `tracking`, `custom` and `ia32` branches mirror roytam1 and are never
edited here; `tuatara` merges `ia32` regularly. Tuatara's own commits carry a
prefix naming the area they touch, such as `[apiaudit]`, `[ntcompat]` or `[docs]`.

## Relationship to other projects

Tuatara is not affiliated with Moonchild Productions (UXP, Pale Moon), the
Basilisk project, or roytam1's Serpent and New Moon builds. "Pale Moon",
"Basilisk" and "Serpent" are their owners' names and are not used for this
browser. Upstream's original README is in `git show tracking:README.md`.

## Security

These versions of Windows have had no security updates for decades and lack
protections such as ASLR and DEP. A current browser makes the web usable on
them; it can't make the OS safe. Use Tuatara for hobby and retro use behind a
NAT router, not for banking or other sensitive accounts.

## License

Tuatara is under the [Mozilla Public License 2.0](https://mozilla.org/MPL/2.0/),
the same as UXP. Third-party code in the tree keeps its own license; see
`toolkit/content/license.html`.
