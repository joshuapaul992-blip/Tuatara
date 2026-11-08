# API baselines

Each folder lists the exports of the system DLLs on one target OS, made with
[`apiaudit media` and `apiaudit baseline`](../tools/apiaudit/README.md) from
licensed install media. Only export names, ordinals, forwarders and file
versions are stored here, never the DLLs themselves.

| Folder | System | Status |
|---|---|---|
| `nt351-sp5/` | Windows NT 3.51 SP5 (3.51, build 1057) | Needed |
| `nt4-sp6a/` | Windows NT 4.0 SP6a (4.0, build 1381) | Needed |
| `w2k-sp4/` | Windows 2000 SP4 (5.0, build 2195) | Needed |

Make baselines from a clean system: base media plus the service pack, with no
Internet Explorer update. A baseline with IE installed may be added under a
separate id (for example `nt4-sp6a-ie6`) for information, but CI checks the
clean ones.
