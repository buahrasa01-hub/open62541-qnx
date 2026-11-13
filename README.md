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
  a host client (`tools/host_client.c`): read all six nodes, write the
  setpoint + fan speed with read-back, subscribe and receive data-change
  notifications (see "Demo" below).
- **Encrypted round-trip (stretch goal, verified):** with
  `UA_ENABLE_ENCRYPTION=OPENSSL` (SDP-bundled OpenSSL 3.0.20) the library
  builds clean, and `tests-on-target/test_encryption.c` passes on target —
  a secure-only server (Basic256Sha256, SignAndEncrypt, no None endpoint)
  and a client that reads a value back over the encrypted channel
  (see "Encryption" below). The demo server itself still ships security
  None; the encrypted build is a separate, verified configuration.

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
scripts/build-encryption.sh     same, with UA_ENABLE_ENCRYPTION=OPENSSL + test_encryption
scripts/build-target.sh         build demo server + on-target tests with qcc
scripts/build-host-client.sh    build the host client (native gcc + amalgamation)
examples/server_qnx_sensors.c   demo OPC UA server (sensors + writable setpoint/fan)
tests-on-target/                on-target tests (no libcheck dependency), incl. test_encryption.c
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

## Demo

On the target (after booting the QEMU image — SSH on the forwarded port;
the guest port 4840 is forwarded to the host):

```sh
./server_qnx_sensors 120     # run 120 s (0 = until Ctrl-C)
```

From the host:

```sh
./host_client opc.tcp://127.0.0.1:4840
```

What it checks (exit 0 only if every step passes): connect; read all six
nodes; write Setpoint = 24.0 and FanSpeed = 75.0 with read-back; subscribe
to Temperature + Count and collect data-change notifications for 5 s.
Verified output (2026-11-13, QNX 8.0 under QEMU):

```
PASS: connected to opc.tcp://127.0.0.1:4840
READ Temperature=22.27
READ Vibration=1.56
READ Count=9
READ Setpoint=22.00
READ FanSpeed=50.00
READ Alarm=false
PASS: all six nodes readable
READ Setpoint-after-write=24.00
PASS: Setpoint write/read-back 24.0
READ FanSpeed-after-write=75.00
PASS: FanSpeed write/read-back 75.0
PASS: subscription created (id=1)
PASS: monitored items created (ids 1, 2)
NOTIFY Temperature=22.39
NOTIFY Temperature=22.49
...
NOTIFY Temperature=22.84
INFO: Temperature notifications=10, Count=10
PASS: Temperature notifications >= 2
PASS: Count notifications >= 2
host-client: ALL PASS
```

Note the Temperature notifications climbing 22.39 → 22.84: after the
Setpoint write to 24.0 the temperature visibly converges toward it —
the first-order lag, not a jump.

## Demo server nodes

Under `Objects/ProductionLine`, namespace 1:

| NodeId | Name | Type | Access |
|--------|------|------|--------|
| ns=1;i=1001 | Temperature | Double (°C) | read |
| ns=1;i=1002 | Vibration | Double (mm/s) | read |
| ns=1;i=1003 | Count | UInt32 (parts) | read |
| ns=1;i=1004 | Setpoint | Double (°C) | read + write |
| ns=1;i=1005 | FanSpeed | Double (%) | read + write |
| ns=1;i=1006 | Alarm | Boolean | read |

Values update every 500 ms. Temperature is a state variable that
approaches the Setpoint with first-order lag (faster with a higher
FanSpeed) plus small process noise; Vibration rises with FanSpeed; Alarm
trips when Temperature exceeds Setpoint + 3 °C or Vibration exceeds
2.5 mm/s. Shutdown is clean: Ctrl-C / SIGTERM stops the server loop,
and the timed mode runs `UA_Server_run_shutdown` before exit.
Security: None (this build uses `UA_ENABLE_ENCRYPTION=OFF` — the
encrypted configuration is verified separately, see "Encryption").

## Encryption (verified stretch goal)

The demo ships security None, but the encrypted configuration is real and
verified on target — it is not a roadmap item:

```sh
scripts/build-encryption.sh    # -> build-encryption/install/lib/libopen62541.a
                               # -> build-encryption/target/test_encryption
```

- OpenSSL comes from the SDP itself: **OpenSSL 3.0.20** bundled in SDP 8.0
  (headers in `$QNX_TARGET/usr/include/openssl`, libs in
  `$QNX_TARGET/x86_64/usr/lib/libcrypto.so` / `libssl.so`). No dashboard
  download is needed, and the target image already ships `libcrypto.so.3`
  and `libssl.so.3` in the IFS.
- Build quirk: CMake's `FindOpenSSL` finds the headers and version from
  the sysroot but **not** the libraries (they live in the arch-specific
  `x86_64/usr/lib`, which `FindOpenSSL` does not search) — configure fails
  with `missing: OPENSSL_CRYPTO_LIBRARY` unless both libraries are passed
  explicitly; the script does that.
- `tests-on-target/test_encryption.c` generates two self-signed
  certificates in-process (RSA-2048, 14 days), starts a **secure-only**
  server on port 4841 (`Basic256Sha256`, `SignAndEncrypt`, no None
  endpoint), and connects a client configured the same way; it reads
  `TestValue = 42` back over the encrypted channel. Verified on target
  2026-11-13: `encryption: ALL PASS`. Runtime is ~90 s under QEMU/TCG,
  mostly the two RSA-2048 key generations.
- Two gotchas worth knowing (both in the test's comments):
  `UA_ServerConfig_setDefaultWithSecureSecurityPolicies` resets the
  config first, so the server's `ApplicationUri` must be set **after**
  that call — setting it before is silently wiped. And the client's
  `ApplicationUri` must match the URI in its certificate, or the server
  rejects the session with `BadCertificateInvalid` at `CreateSession`.
- The `EccNistP256` "Could not add SecurityPolicy" warnings at startup are
  benign: the RSA test certificate cannot back an ECC policy, so that
  policy is simply not offered.

## Codelab

The porting process is taught step by step in the companion codelab
**"Port a POSIX C Library to QNX 8.0 under QEMU"** (official QNX
codelab format, source in `docs/codelab/port-open62541-qnx.md`):

- **Read it:** https://buahrasa01-hub.github.io/open62541-qnx/codelab/port-open62541-qnx.html
- 9 steps, ~60 minutes: licence + SDP install, the `qcc` toolchain,
  a cross-compiled hello, a minimal headless QEMU image, the
  library's build system, the toolchain file and defects G1–G4,
  on-target proof, and packaging the port (plus an optional
  encryption section). Every command is copy-pasteable; every
  expected output shown is real, captured from the 2026-11-13 run.

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
