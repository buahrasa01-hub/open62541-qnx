# Target verdict summary (2026-11-13)

Locked target: **open62541 v1.5.1**, category Open-Source Contribution &
Ports + Best Codelab bonus. Full evidence: `target-verdict.md` in the parent
workdir (`~/workspace/overnight/qnx/`).

## Why open62541

- oss.qnx.com dashboard: **no** open62541 package (X-Count 0). A QNX 8.0
  port is new work, not a duplicate.
- Upstream v1.5.x carries only a Tier-3 QNX entry (PR #6967, 25 lines,
  2025-02-20, no CI, no QNX version targeted in code). No `arch/qnx/`
  directory; QNX builds as `posix`.
- Mosquitto is already shipped by QNX (qnx_v2.0.15, SDP 8.0, build_passed)
  — eliminated. libcoap is the backup (upstream QNX support more complete,
  thinner story).

## The concrete gap (proven by a direct qcc build probe)

| # | Defect | Fix |
|---|--------|-----|
| G1 | `#include <bits/stdio_lim.h>` (glibc-only) in `arch/posix/eventloop_posix.h` | Exclude `__QNX__` |
| G2 | Bundled `open62541_queue.h` skipped on QNX, no replacement → `LIST_ENTRY` undefined | `#include <sys/queue.h>` on `__QNX__` |
| G3 | Links `-lpthread -lrt` (no separate libs on QNX) | QNX exclusion in CMakeLists |

Non-issues (verified): epoll → `select()` fallback is automatic; raw-Ethernet
PubSub is Linux-guarded; `sys/inotify.h` exists in SDP 8.0.

## Week-2 gate

Library + demo server running on the QNX target under QEMU. Pivot decision
(libcoap probe vs host-side `qnx-port-check` fallback) by 2026-11-28 if the
demo is not running on target.
