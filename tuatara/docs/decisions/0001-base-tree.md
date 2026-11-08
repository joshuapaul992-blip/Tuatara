# 0001: Base on roytam1's `ia32` branch

**Status:** Accepted

## Decision

Tuatara's engine starts from the `ia32` branch of
[roytam1/UXP](https://github.com/roytam1/UXP) and keeps merging it.

## Why

roytam1's tree has three relevant branches, all kept current:

- `tracking` mirrors upstream UXP;
- `custom` adds the Windows XP and Vista work;
- `ia32` adds fixes for builds without SSE2, and is merged from `custom`
  within days of each upstream change.

Building on `ia32` means XP-level API work and x87-only builds are already
done. Its own commits include a Skia fix for SSE code paths running on CPUs
the build didn't expect, which is the class of problem Tuatara has to solve
on NT 4.0.

Alternatives considered:

- **Upstream UXP and Basilisk:** Windows 7 and later only, and upstream
  Basilisk has no WebExtensions.
- **Eclipse Hydra**, another Basilisk fork for XP: less established.

## Consequences

- Tuatara follows roytam1's merge-based flow rather than rebasing.
- Patches below the XP level should be offered to roytam1 where they fit.
- The front end starts from `application/basilisk`, which is in the same tree.
