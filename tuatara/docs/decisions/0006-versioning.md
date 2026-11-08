# 0006: Versioning

**Status:** Proposed

## Decision

- **Tuatara** uses date versions, `YYYY.MM.DD` (for example `2027.01.15`), as
  Basilisk and Serpent do. A second build on the same day adds `.1`.
- **Quentix** uses `MAJOR.MINOR`, starting at `1.0`; the minor number goes up
  with each merge of roytam1's `ia32`.
- Every build records the UXP base commit and shows it in the About dialog.

## Why

Releases follow upstream's cadence rather than feature milestones, so a date
says the most about how current a build is. The engine number makes it easy
to say which engine a bug report is about.

## Open

Semantic versions (`1.0`, `1.1`, …) for Tuatara are the alternative, if
releases end up feature-driven.
