# 0004: Tuatara is the browser, Quentix the engine

**Status:** Accepted

## Decision

- **Tuatara** is the browser. The tuatara is a New Zealand reptile often called
  a "living fossil", which suits a current browser on 1990s Windows and
  continues the reptile names of Basilisk and Serpent without copying them.
  `tuatara.exe` fits the 8.3 file name limit.
- **Quentix** is the engine, in the way Goanna is UXP's engine name.

## Where the names appear

| Place | Text |
|---|---|
| About dialog | "Tuatara 2027.01.15 — powered by Quentix 1.0 (based on UXP)" |
| Executable, profile folder | `tuatara.exe`, `Tuatara` |
| Default user agent | Firefox-compatible, **without** a Quentix token |
| Optional user agent | A preference adds `Quentix/<version>` |

The engine name stays out of the default user agent because Pale Moon's
Goanna token caused enough site breakage that it added a Firefox-compatible
mode. Whether to report a newer Windows version than NT 4.0 or NT 3.51 (some
sites refuse "unsupported" systems) is decided later.

## Names avoided

Microsoft trademarks (Windows, Win, NT), Mozilla trademarks (Fire-, -fox,
Gecko), and names close to related projects (Pale Moon, Basilisk, Serpent,
Hydra, Supermium). Before the first public release, do a trademark search for
both names.

## Follow-up

The tree still says "Goanna" in places, mainly user-agent code and about:
strings. Those are inventoried and renamed in M3. Identifiers the build
depends on are left alone.
