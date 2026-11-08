# 0005: One binary for every Windows

**Status:** Proposed

## Decision

Ship one i686 build that runs on Windows NT 3.51 through Windows 11. Code
paths are chosen at runtime from a capability record (for example: has
layered windows, has Winsock 2, has CryptoAPI, OS saves SSE state), not with
compile-time OS switches.

## Why

- One build, one package and one release to test and publish.
- Development can happen on modern Windows at full speed: a capability
  emulation switch (`NTCOMPAT_EMULATE=nt4`) makes `ntcompat` behave as if
  newer APIs were missing, and the VMs confirm.
- UXP already selects some backends at runtime on Windows (for example
  DirectWrite or GDI), so the pattern fits.

## Costs

- Some code is present but unused on any given system.
- Every fallback path must be exercised by tests, because no build "only"
  targets the old systems.
- The PE subsystem version must be 3.10 so NT 3.51 accepts the files.
