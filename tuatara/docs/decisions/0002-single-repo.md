# 0002: One repository

**Status:** Accepted

## Decision

Everything lives in `joshuapaul992-blip/Tuatara`, a GitHub fork of
roytam1/UXP:

- the engine, as in roytam1's tree;
- the front end in `application/tuatara/` (UXP keeps applications in the tree);
- the compatibility layer in `ntcompat/`;
- docs, tools, baselines and CI in `tuatara/`.

## Branches

| Branch | Rule |
|---|---|
| `tracking`, `custom`, `ia32`, others from roytam1 | Mirrors. Never commit to them; update with GitHub's "Sync fork". |
| `tuatara` | Our work. The default branch. Merges `ia32` on each upstream round. |

Tuatara's commits start with a prefix naming the area: `[apiaudit]`,
`[ntcompat]`, `[gfx-ft2]`, `[widget]`, `[branding]`, `[docs]`, `[ci]` and so on.
That keeps them easy to find among upstream history and to offer upstream.

## Why

A separate project repo with the engine as a submodule was considered. A single
fork is simpler: one issue tracker, one place to look, and syncing and pull
requests to roytam1 still work through GitHub's fork features.
