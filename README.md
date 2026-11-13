# open62541 on QNX 8.0

An OPC UA stack for QNX 8.0: the open-source [open62541](https://github.com/open62541/open62541)
(v1.5.1) ported to QNX SDP 8.0 (x86_64), with on-target verification under QEMU
and a demo OPC UA server exposing simulated production-line sensors.

This is the submission repo for the **QNX Global Hackathon 2026** — category
**Open-Source Contribution & Ports** (+ Best Codelab bonus). It is **not** a fork
of upstream: it contains only the porting work (patches, QNX toolchain file,
build scripts, demo server, on-target tests) and references upstream by URL.
The upstream fixes are also offered back as a pull request (see below).

## The gap

Upstream open62541 v1.5.1 does **not** compile for QNX 8.0 today, although it
carries a Tier-3 QNX entry (PR #6967, 25 lines, 2025-02-20). The QNX Open-Source
Dashboard (oss.qnx.com) lists **no** open62541 package at all. A direct
`qcc` build fails in three places, all proven by the build probe (see
`docs/` and the target verdict in the parent workdir):

| # | Location | Defect | Fix |
|---|----------|--------|-----|
| G1 | `arch/posix/eventloop_posix.h:276-282` | `#include <bits/stdio_lim.h>` — a glibc-internal header, absent on QNX | Exclude `__QNX__` from the include (`FILENAME_MAX` already comes from QNX `<stdio.h>`) |
| G2 | `arch/posix/eventloop_posix.h:21-22` | The bundled `open62541_queue.h` is skipped on QNX, but nothing includes the system header → `LIST_ENTRY` / `LIST_HEAD` undefined | `#include <sys/queue.h>` on the `__QNX__` path |
| G3 | `CMakeLists.txt:497-503` | Links `-lpthread -lrt`, which do not exist as separate libraries on QNX (both are in libc) | QNX exclusion, following the existing APPLE / OpenBSD / Android exclusions |
| G4 | `CMakeLists.txt:501-510` | Socket API (`socket`, `getaddrinfo`, `setsockopt`, ...) lives in a separate `libsocket` on QNX, not in libc — every executable linking the library failed with undefined references | Link `socket` on QNX, following the win32 precedent (`ws2_32`) |

That is the whole upstream delta: **four small fixes + a CMake toolchain file**.
The engineering depth is everything around it: on-target tests, a demo
server, and a real client↔server round-trip on QNX-in-QEMU.

## What is verified (not just cross-compiled)

- `libopen62541.a` builds clean from this repo alone (no shims) — genuine
  x86-64 QNX ELF relocatables, `UA_Server_new` present.
- The single-file **amalgamation** (`open62541.c/.h`) also builds under `qcc`.
- On-target tests (run on QNX 8.0 under QEMU, no KVM):
  - `test_types_roundtrip` — binary encode/decode round-trips (Int32, Double,
    String, Boolean, DateTime, arrays) + NodeId equality.
  - `test_eventloop` — the POSIX eventloop `select()` fallback fires timers
    (no epoll on QNX): 100 ms / 250 ms repeating timers over ~2.1 s.
  - `test_server_client` — in-process server + client round-trip over
    loopback (connect, read value back).
- Demo server `examples/server_qnx_sensors.c` runs on the target and serves
  a host client (`tools/host_client.c`): read all four nodes, write the
  setpoint + read-back, subscribe and receive data-change notifications.

Upstream unit tests that need the `libcheck` framework are **not** run on
target — `libcheck` itself is not available for QNX 8.0. The tests in
`tests-on-target/` cover the same API surface (types, eventloop, server /
client) without that dependency.

## Repo layout

```
patches/                        G1 + G2 + G3 + G4 as clean diffs vs upstream v1.5.1
cmake/toolchain-qnx-x86_64.cmake
                                qcc + QNX system name (drops -lpthread/-lrt via G3)
scripts/build.sh                clone v1.5.1 -> apply patches -> build lib + amalgamation
scripts/build-target.sh         build demo server + on-target tests with qcc
scripts/build-host-client.sh    build the host client (native gcc + amalgamation)
examples/server_qnx_sensors.c   demo OPC UA server (sensors + writable setpoint)
tests-on-target/                on-target tests (no libcheck dependency)
tools/host_client.c             host client: read / write / subscribe
docs/                           notes (target verdict summary, codelab outline)
```

## Build (QNX target)

Prerequisites: QNX SDP 8.0 installed (this repo was built with
8.0.0.00141T202311271501L), `qcc` on `PATH`
(`source ~/qnx/qnx800/qnxsdp-env.sh`), `cmake >= 3.16`, `git`, `python3`.

```sh
scripts/build.sh                 # -> build/install/lib/libopen62541.a
                                 # -> build/build/open62541.c + open62541.h
scripts/build-target.sh          # -> build/target/server_qnx_sensors, test_*
scripts/build-host-client.sh     # -> build/host/host_client
```

The build works from this repo alone: the only upstream input is the
`v1.5.1` tag, fetched from https://github.com/open62541/open62541.git.

## Run on the QEMU target

Build a minimal headless QNX image with `mkqnximage --type=qemu` (see the
codelab), copy the target binaries into the image (or `scp` them in after
boot — SSH on the forwarded port), then on the target:

```sh
./test_types_roundtrip
./test_eventloop
./test_server_client
./server_qnx_sensors 120     # run 120 s (0 = until Ctrl-C)
```

From the host (guest port 4840 forwarded):

```sh
./host_client opc.tcp://127.0.0.1:4840
```

## Demo server nodes

Under `Objects/ProductionLine`, namespace 1:

| NodeId | Name | Type | Access |
|--------|------|------|--------|
| ns=1;i=1001 | Temperature | Double (°C) | read |
| ns=1;i=1002 | Vibration | Double (mm/s) | read |
| ns=1;i=1003 | Count | UInt32 (parts) | read |
| ns=1;i=1004 | Setpoint | Double (°C) | read + write |

Values update every 500 ms; Temperature drifts toward the Setpoint.
Security: None (this build uses `UA_ENABLE_ENCRYPTION=OFF`; encrypted
endpoints via the QNX dashboard's OpenSSL 3.6.3 are a stretch goal).

## Upstream

- Upstream: https://github.com/open62541/open62541 (MPL-2.0)
- Fixes offered upstream: **PR [#8717](https://github.com/open62541/open62541/pull/8717)**
  ("fix(arch): build the POSIX architecture on QNX 8.0 (1.5 backport)") —
  G2+G3+G4 against the `1.5` branch. G1 is already on 1.5 via upstream
  `2c9beadfc` (stdio_lim include removed entirely). Note: upstream `master`
  already carries the equivalent QNX 8.0 fix (`667acbc69`, 2026-11-11) — this
  PR is the backport to the 1.5 maintenance line, which still lacked it.
- Known non-issues (verified, not assumed): epoll (the `select()` fallback
  is automatic), raw-Ethernet PubSub (Linux-guarded, out of scope),
  `sys/inotify.h` (present in SDP 8.0).

## Licence

Mozilla Public License 2.0 (same as upstream) — see `LICENSE`. The demo
server, tests, and scripts in this repo are also MPL-2.0.
