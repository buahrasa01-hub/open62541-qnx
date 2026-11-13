---
id: port-open62541-qnx
title: Port a POSIX C Library to QNX 8.0 under QEMU
summary: A step-by-step guide to porting a real POSIX C library (open62541, an OPC UA stack) to QNX SDP 8.0 — cross-compiling with qcc, booting a minimal headless QEMU image, fixing the four build defects, and proving the port on target
categories: qnx, porting, cmake, opc-ua
tags: intermediate
difficulty: 3
status: published
authors: Bayu Luqman Fadilah
feedback_link: https://github.com/buahrasa01-hub/open62541-qnx/issues
---

# Port a POSIX C Library to QNX 8.0 under QEMU

## Introduction

Duration: 2:00

Most POSIX C libraries *almost* build for QNX. The gap is usually small —
a glibc-only header here, a link flag there — but finding it means
cross-compiling, booting a target, and proving the result runs. This
codelab walks the whole loop once, end to end, using a real port as the
worked example: **open62541 v1.5.1**, an open-source OPC UA stack, brought
up on **QNX SDP 8.0 (x86_64)** and verified on a QEMU target.

By the end you will have: a QNX 8.0 toolchain that cross-compiles, a
minimal headless QEMU image you can SSH into, a CMake toolchain file,
four small upstreamable patches, on-target tests, and an OPC UA demo
server running on QNX serving a host client.

**What you will learn:**

* How to get the free QNX Everywhere licence and install SDP 8.0
* How the `qcc` cross-compiler and the QNX sysroot are laid out
* How to build a minimal headless QEMU image with `mkqnximage` (and why
  not the graphical quick-start image)
* How to read a library's build system and find its platform touchpoints
* How to fix the four classic QNX porting defects: a glibc-only header,
  a missing system header, phantom link libraries, and a split socket
  library
* How to prove a port on target — not just that it compiles, but that
  it runs, serves, and encrypts

**Prerequisites:**

* A Linux x86_64 host (this codelab was written on Ubuntu 24.04),
  with `git`, `cmake >= 3.16`, `python3`, and `ssh` installed
* QEMU (`qemu-system-x86_64`) — version 8.2.2 was used here; any
  recent QEMU works for a headless image
* A free myQNX account (step 1 walks through it)
* No QNX experience assumed; C and CMake familiarity expected

> **Note:** Every command in this codelab is copy-pasteable as one clean
> block, and every expected output shown is real — captured from the
> actual porting run on 2026-11-13. Nothing here is invented. Where a
> step can fail in a known way, the failure and the fix are shown too.

> **A note on honesty:** upstream open62541 already carried a Tier-3
> QNX entry (PR #6967, 25 lines) — and upstream `master` fixed the
> QNX 8.0 build on 2026-11-11, two days before this port. This codelab
> does not claim to be the first fix. What it *does* show, verifiably,
> is that v1.5.1 did not compile for QNX 8.0, the exact four defects
> that stopped it, and the on-target proof that the ported 1.5.1
> library works. The fixes were offered upstream as
> [PR #8717](https://github.com/open62541/open62541/pull/8717), a
> backport to the 1.5 maintenance line.

**The worked example:** all patches, scripts, tests, and the demo
server live in the companion repo
[buahrasa01-hub/open62541-qnx](https://github.com/buahrasa01-hub/open62541-qnx)
(MPL-2.0, not a fork of upstream). Upstream is
[open62541/open62541](https://github.com/open62541/open62541), tag
`v1.5.1`.

---

## Step 1 — Get the licence and the tools

Duration: 5:00

QNX SDP 8.0 is free for non-commercial use through the **QNX
Everywhere** programme. The licence lives in your myQNX account; the
Software Center downloads and installs the SDP on your Linux host.

1. Create a free myQNX account at
   <https://qnx.com/developers/docs/qnxeverywhere> (the QNX Everywhere
   page) and complete the licence form while logged in.
2. Open the License Manager in your myQNX dashboard, accept the
   licence, and deploy it to your own myQNX email. QNX says to allow
   up to one hour for it to appear; in practice it was there within
   minutes.
3. Download and install the **QNX Software Center** for Linux, sign
   in with your myQNX account, and install **QNX SDP 8.0** (plus the
   QEMU quick-start package `com.qnx.qnx800.quickstart.qemu`, which
   provides `mkqnximage`). The install is about 3.5 GB.

**Checkpoint:** the SDP is installed and the installation verifies
clean:

```bash
ls ~/qnx/qnx800
# host/  qnxsdp-env.bat  qnxsdp-env.sh  target/
```

```bash
~/qnx/qnxsoftwarecenter/qnxsoftwarecenter_clt -verifyInstallation
# ... Errors: 0
```

> **Gotcha — the licence file lives in two places.** The Software
> Center (a Java app) stores licences under `/root/.qnx/license/`
> (`user.home=/root`), but `qcc` reads `$HOME/.qnx/license/`. Until
> the file is copied, `qcc` fails with "no valid license". Fix it
> once:
>
> ```bash
> sudo cp /root/.qnx/license/licenses ~/.qnx/license/licenses
> ```

---

## Step 2 — Meet the toolchain

Duration: 5:00

The SDP ships its own environment script. Source it once per shell —
it puts `qcc` on `PATH` and sets `QNX_HOST` / `QNX_TARGET` (the
compiler host tools and the target sysroot).

```bash
source ~/qnx/qnx800/qnxsdp-env.sh
echo "QNX_HOST=$QNX_HOST"
echo "QNX_TARGET=$QNX_TARGET"
```

```
QNX_HOST=/home/hatch/qnx/qnx800/host/linux/x86_64
QNX_TARGET=/home/hatch/qnx/qnx800/target/qnx
```

`qcc` is a driver that dispatches to the right GCC for a target.
List the targets it knows:

```bash
qcc -V
```

```
cc: targets available in /home/hatch/qnx/qnx800/host/linux/x86_64/etc/qcc:
        12.2.0,gcc_ntox86_64_cxx
        12.2.0,gcc_ntoaarch64le
        12.2.0,gcc_ntox86_64_gpp
        12.2.0,gcc_ntox86_64      (default)
        12.2.0,gcc_ntoaarch64le_cxx
        12.2.0,gcc_ntoaarch64le_gpp
```

**Checkpoint:** `qcc -V` lists `12.2.0,gcc_ntox86_64`. That is the
compiler for the rest of this codelab: `qcc -Vgcc_ntox86_64` selects
GCC 12.2.0 targeting QNX 8.0 on x86_64. (There is no KVM requirement
anywhere in this codelab — the target runs under QEMU's pure emulation,
TCG, which is slow but workable for small binaries.)

---

## Step 3 — Hello, QNX (cross-compiled)

Duration: 5:00

Before touching a library, prove the toolchain with a 20-line program
that uses one unmistakably QNX-only call — `ClockTime()` from
`<sys/neutrino.h>` — so the binary cannot be mistaken for a Linux
build.

```c
/* hello.c */
#include <stdio.h>
#include <sys/neutrino.h>
#include <inttypes.h>
#include <time.h>

int main(void) {
    _Uint64t now_ns = 0;
    struct timespec ts;
    ClockTime(CLOCK_REALTIME, NULL, &now_ns);   /* QNX-only API */
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("hello from QNX: realtime = %" PRIu64 " ns, monotonic = %" PRIu64 " ns\n",
           now_ns,
           (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec);
    return 0;
}
```

Build it:

```bash
source ~/qnx/qnx800/qnxsdp-env.sh
qcc -Vgcc_ntox86_64 -o hello hello.c
```

**Checkpoint:** `file` reports a genuine QNX ELF — note the
interpreter, `ldqnx-64.so.2`, which exists only on QNX:

```bash
file hello
```

```
hello: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),
dynamically linked, interpreter /usr/lib/ldqnx-64.so.2, with debug_info,
not stripped
```

If `file` says `interpreter /lib64/ld-linux-x86-64.so.2` instead, you
compiled with the host `gcc` — re-check that `qnxsdp-env.sh` was
sourced and that `qcc` is first on `PATH`.

---
## Step 4 — A target you can boot in QEMU

Duration: 10:00

The stock QNX Quick Start Target Image (QSTI) is a graphical desktop
whose `mkqnximage --run` defaults are 8 vCPUs and 4 GB of RAM — a poor
fit for a host without KVM. Instead, build a **minimal headless
image**: one CPU, 256 MB of RAM, SSH key login, and a serial console.
It boots in about a minute under TCG and is all a porting workflow
needs.

```bash
source ~/qnx/qnx800/qnxsdp-env.sh
mkdir -p ~/qnx-work/vm && cd ~/qnx-work/vm
mkqnximage --type=qemu --cpu=1 --ram=256M \
  --ssh-ident=$HOME/.ssh/id_ed25519.pub \
  --data-size=2000 --noprompt
```

The build takes ~20 seconds and produces a ~2.1 GB disk image and an
11 MB `output/ifs.bin` (the boot image). Boot it headless with QEMU.
This host has no bridge network (`br0`/`virbr0` absent), so use
slirp user-mode networking with host port forwards — 2222 to the
guest's SSH port 22, and 4840 to the guest's OPC UA port 4840 (used
in step 8):

```bash
qemu-system-x86_64 -smp 1 --cpu Haswell -m 256M \
  -drive file=output/disk-qemu.vmdk,if=ide,id=drv0 \
  -netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::4840-:4840 \
  -device virtio-net-pci,netdev=net0 \
  -pidfile output/qemu.pid -nographic \
  -kernel output/ifs.bin -serial file:output/qemu.out
```

> **CPU choice matters.** `-cpu qemu64` stalls in SeaBIOS for over a
> minute; `--cpu max` boots fastest (~55 s) but the stock image's
> `sshd` segfaulted under it while probing AVX512. `-cpu Haswell`
> (no AVX512) is the reliable choice; full boot plus auto-diagnostics
> completes within ~100 s of launch.

Wait for `Startup complete` in the serial log, then SSH in from the
host (the guest gets DHCP `10.0.2.15` from slirp, ~15 s after boot):

```bash
ssh -i ~/.ssh/id_ed25519 -p 2222 root@127.0.0.1
```

**Checkpoint:** the target answers, and `uname -a` confirms QNX
8.0.0 on x86_64.

> **Two SSH gotchas, found the hard way.** The stock image's `sshd`
> has two defects, both fixed in the companion repo's image
> snippets (`vm/local/snippets/`):
>
> 1. **The RSA host key segfaults `sshd`** (exit 139) while loading
>     — under every CPU flag and config tried. Fix: ed25519 host key
>     only (`sshd -t` then exits 0).
> 2. **The stock image ships `sshd-session` but not `sshd-auth`.**
>     OpenSSH 10.4 refuses to start without it. Fix: add
>     `bin/sshd-auth=usr/libexec/sshd-auth` via
>     `system_files.custom` and set `SshdAuthPath
>     /system/bin/sshd-auth` in the config.
>
> And one shell quirk: SSH sessions get a minimal `PATH` — prefix
> remote commands with `export PATH=/proc/boot:/system/bin:$PATH`
> (toybox applets live in `/proc/boot`, system binaries in
> `/system/bin`).

> **Sidebar — when *would* you use the graphical image?** When you
> need the QNX Developer Desktop, a GUI demo, or Vulkan/720p
> graphics. For a library port, the headless image is faster to
> build, faster to boot, and scriptable — and the demo video can be
> terminal footage.

---

## Step 5 — Run hello on the target

Duration: 5:00

Copy the binary from step 3 into the guest over SSH and run it:

```bash
scp -i ~/.ssh/id_ed25519 -P 2222 hello root@127.0.0.1:/data/home/root/
ssh -i ~/.ssh/id_ed25519 -p 2222 root@127.0.0.1 \
  'export PATH=/proc/boot:/system/bin:$PATH; /data/home/root/hello'
```

**Checkpoint:** the QNX clock value prints from inside QEMU:

```
hello from QNX: realtime = 1794571344630089469 ns, monotonic = 22157871460 ns
```

(The numbers will differ on your run; the shape is the point.) You
now have the full loop a port needs: edit on the host, cross-compile,
copy, run on target. Everything after this is the same loop with a
bigger program.

---

## Step 6 — Pick a portable library and read its build system

Duration: 5:00

A good porting candidate is a POSIX C library with a CMake build, a
single-file (amalgamation) option, and no GUI. This codelab's example
is **open62541 v1.5.1** — an open-source OPC UA stack (C99, MPL-2.0)
used in industrial and automotive middleware, a direct line to QNX's
own markets.

Before writing any patch, check three things:

1. **Is it already ported?** Query the QNX Open-Source Dashboard
   (oss.qnx.com). At the time of this port, `open62541` returned
   zero packages — genuinely absent, not a duplicate. (For contrast,
   Mosquitto was already listed as `qnx_v2.0.15` for SDP 8.0 — a
   port there would have been a version bump at best.)
2. **What does upstream already claim?** open62541's `arch/README.md`
   lists QNX as **Tier 3** — no CI, no `arch/qnx/` directory; QNX
   builds as `posix`. The entire upstream QNX support is one 2025
   commit (PR #6967, 25 insertions). No QNX version is targeted in
   code.
3. **What are the platform touchpoints?** Read the build system
   (`CMakeLists.txt`) and the platform layer (`arch/posix/`). For
   open62541 they are: **sockets** (TCP/UDP, `select()`/`epoll`),
   **threads** (pthreads vs. the platform's model), and **time**
   (monotonic clocks, timers). Every porting defect you will meet
   lives in one of those three places.

Two non-issues worth confirming early, so you do not chase them:

* **epoll:** `UA_HAVE_EPOLL` is defined only `#if
  defined(__linux__)` — on QNX the eventloop takes the `select()`
  fallback automatically. No code change; a real behaviour
  difference worth measuring on target (step 8 does).
* **inotify:** `sys/inotify.h` exists in SDP 8.0, so the
  certificate-filestore watcher is not a blocker.

**Checkpoint:** you can name the library's three platform
touchpoints (sockets, threads, time) and state, with evidence,
whether a port already exists.

---
## Step 7 — Cross-build with a CMake toolchain file

Duration: 10:00

Write a toolchain file that tells CMake the target is QNX and the
compiler is `qcc`. This is the whole file used in the companion repo
(`cmake/toolchain-qnx-x86_64.cmake`):

```cmake
set(CMAKE_SYSTEM_NAME QNX)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# SDP location: prefer the environment from qnxsdp-env.sh; fall back to
# the default install path under $HOME.
if(NOT DEFINED ENV{QNX_HOST} OR NOT DEFINED ENV{QNX_TARGET})
    set(_QNX_SDP_DEFAULT "$ENV{HOME}/qnx/qnx800")
    if(EXISTS "${_QNX_SDP_DEFAULT}/qnxsdp-env.sh")
        set(ENV{QNX_HOST} "${_QNX_SDP_DEFAULT}/host/linux/x86_64")
        set(ENV{QNX_TARGET} "${_QNX_SDP_DEFAULT}/target/qnx")
    endif()
    unset(_QNX_SDP_DEFAULT)
endif()

# Compilers: full path when the SDP is known, plain name otherwise.
if(DEFINED ENV{QNX_HOST} AND EXISTS "$ENV{QNX_HOST}/usr/bin/qcc")
    set(CMAKE_C_COMPILER "$ENV{QNX_HOST}/usr/bin/qcc")
    set(CMAKE_CXX_COMPILER "$ENV{QNX_HOST}/usr/bin/QCC")
else()
    set(CMAKE_C_COMPILER qcc)
    set(CMAKE_CXX_COMPILER QCC)
endif()
set(CMAKE_C_COMPILER_ARG1 -Vgcc_ntox86_64)
set(CMAKE_CXX_COMPILER_ARG1 -Vgcc_ntox86_64)

# Find libraries and headers in the QNX target sysroot only.
if(DEFINED ENV{QNX_TARGET})
    set(CMAKE_FIND_ROOT_PATH "$ENV{QNX_TARGET}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

Configure and build against upstream v1.5.1:

```bash
git clone --depth 1 --branch v1.5.1 \
  https://github.com/open62541/open62541.git upstream
cmake -S upstream -B build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-qnx-x86_64.cmake \
  -DUA_ARCHITECTURE=posix -DUA_ENABLE_ENCRYPTION=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
```

CMake configures fine. The core builds to 73%. Then the build fails
— four times, in four distinct places. Each failure is a classic QNX
porting defect, and each fix is a few lines. Here they are, in the
order the build surfaced them:

### Defect G1 — a glibc-only header

```
arch/posix/eventloop_posix.h:280: fatal error: 'bits/stdio_lim.h' file not found
```

`<bits/stdio_lim.h>` is a glibc-internal header; it does not exist on
QNX, where `<stdio.h>` already defines `FILENAME_MAX` (as
`__PATH_MAX`). The include was guarded only for Android and Apple.
Fix — exclude `__QNX__` from the include
(`patches/0001-G1-exclude-QNX-from-bits-stdio_lim-include.patch`):

```diff
 #ifndef __ANDROID__
 #ifndef __APPLE__
+#ifndef __QNX__
 #include <bits/stdio_lim.h>
+#endif /* !__QNX__ */
 #endif /* !__APPLE__ */
 #endif /* !__ANDROID__ */
```

### Defect G2 — a skipped header with no replacement

```
arch/posix/eventloop_posix.h:372: error: use of undeclared identifier 'LIST_ENTRY'
```

Upstream skips its bundled `open62541_queue.h` on QNX on the
assumption that QNX provides the BSD queue macros — but nothing
includes the system header. QNX does ship `<sys/queue.h>`. Fix
(`patches/0002-G2-include-sys-queue-h-on-QNX.patch`):

```diff
-#if !defined(__QNX__)
+#if defined(__QNX__)
+/* QNX ships the BSD queue macros in <sys/queue.h>; use the system header
+ * instead of the bundled copy. */
+# include <sys/queue.h>
+#else
 # include "../deps/open62541_queue.h"
 #endif
```

### Defect G3 — phantom link libraries

```
ld: cannot find -lpthread
ld: cannot find -lrt
```

QNX has no separate `libpthread` or `librt` — both are integrated
into libc. CMake's Unix defaults add them unconditionally. Fix —
exclude QNX, following the existing APPLE / OpenBSD / Android
exclusions (`patches/0003-G3-no-separate-libpthread-librt-on-QNX.patch`):

```diff
-    if(UA_MULTITHREADING GREATER_EQUAL 100 OR UA_BUILD_UNIT_TESTS AND NOT ANDROID_NDK_TOOLCHAIN_INCLUDED)
+    # QNX has no separate libpthread / librt (both are integrated into libc),
+    # so neither may be linked on QNX.
+    if((UA_MULTITHREADING GREATER_EQUAL 100 OR UA_BUILD_UNIT_TESTS AND NOT ANDROID_NDK_TOOLCHAIN_INCLUDED) AND NOT ${CMAKE_SYSTEM_NAME} STREQUAL "QNX")
         list(APPEND open62541_PUBLIC_LIBRARIES "pthread")
         list(APPEND pkgcfg_libsprivate "-lpthread")
     endif()
-    if(NOT APPLE AND (NOT ${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD") AND NOT ANDROID_NDK_TOOLCHAIN_INCLUDED)
+    if(NOT APPLE AND (NOT ${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD") AND NOT ANDROID_NDK_TOOLCHAIN_INCLUDED AND NOT ${CMAKE_SYSTEM_NAME} STREQUAL "QNX")
         list(APPEND open62541_LIBRARIES "rt")
         list(APPEND pkgcfg_libsprivate "-lrt")
     endif()
```

### Defect G4 — a split socket library

```
undefined reference to `socket'
undefined reference to `getaddrinfo'
```

QNX keeps the socket API in a separate `libsocket` — it is not part
of libc. Every executable linking the library fails until it is
linked, following the win32 precedent (`ws2_32`). Fix
(`patches/0004-G4-link-libsocket-on-QNX.patch`):

```diff
+    if(${CMAKE_SYSTEM_NAME} STREQUAL "QNX")
+        # QNX keeps the socket API in a separate libsocket (like ws2_32 on
+        # Windows); it is not part of libc.
+        list(APPEND open62541_LIBRARIES "socket")
+        list(APPEND pkgcfg_libsprivate "-lsocket")
+    endif()
```

With all four patches applied, the build completes. The companion
repo automates the whole sequence — clone, patch, configure, build,
amalgamation, install, verify — in `scripts/build.sh`:

```bash
scripts/build.sh
```

**Checkpoint:** the library exists, is a genuine QNX target artifact,
and exports the server constructor:

```bash
ls -la build/install/lib/libopen62541.a
# ... 116 objects inside
nm build/install/lib/libopen62541.a | grep -m1 UA_Server_new
# ... UA_Server_new present
```

The single-file amalgamation (`open62541.c`, 6.7 MB, plus
`open62541.h`) also builds clean under `qcc` — the path the demo in
step 8 uses. Verify the patches apply cleanly, in order, to a
pristine v1.5.1 checkout before you trust them:
`git apply --check patches/*.patch` on a fresh clone.

---

## Step 8 — Prove it on the target

Duration: 10:00

A cross-compile is not a port. Prove the library *runs* on QNX: a
type round-trip, the `select()` eventloop firing timers, a real
server↔client handshake, and the demo server serving a host client.

Build the target binaries with the companion repo's script (it links
against the library from step 7):

```bash
scripts/build-target.sh
# -> build/target/server_qnx_sensors, test_types_roundtrip,
#    test_eventloop, test_server_client
```

Get the binaries onto the target — either add them to the image
snippets (`vm/local/snippets/data_files.custom`) and rebuild the
image with `mkqnximage --clean` (note: without `--clean`,
`mkqnximage` ignores snippet changes), or `scp` them in after boot
over the forwarded SSH port. Then run the tests on the target:

```bash
./test_types_roundtrip
./test_eventloop
./test_server_client
```

**Checkpoint — all three pass on QNX 8.0 under QEMU:**

* `test_types_roundtrip` — **10/10 PASS** (Variant binary
  round-trips: Int32, Double, String, Boolean, DateTime, arrays;
  NodeId equality).
* `test_eventloop` — **ALL PASS** — the 100 ms timer fired 20–21×
  and the 250 ms timer 8× over ~2.1 s. The POSIX eventloop's
  `select()` fallback works on QNX. (A test bug found along the way:
  the eventloop must be `start()`ed before `run()` — otherwise
  `run()` returns `BADINTERNALERROR` and zero timers fire.)
* `test_server_client` — **ALL PASS** — a real OPC UA handshake on
  target (SecureChannel None, session created and activated,
  `TestValue` read back as 42).

Now the demo. `examples/server_qnx_sensors.c` is an OPC UA server
exposing a simulated production line — six nodes under
`Objects/ProductionLine`, updating every 500 ms:

| NodeId | Name | Type | Access |
|--------|------|------|--------|
| ns=1;i=1001 | Temperature | Double (°C) | read |
| ns=1;i=1002 | Vibration | Double (mm/s) | read |
| ns=1;i=1003 | Count | UInt32 (parts) | read |
| ns=1;i=1004 | Setpoint | Double (°C) | read + write |
| ns=1;i=1005 | FanSpeed | Double (%) | read + write |
| ns=1;i=1006 | Alarm | Boolean | read |

Temperature is a state variable that approaches the Setpoint with
first-order lag (faster with a higher FanSpeed) plus small process
noise; Vibration rises with FanSpeed; Alarm trips when Temperature
exceeds Setpoint + 3 °C or Vibration exceeds 2.5 mm/s. Start it on
the target (the guest's port 4840 is forwarded to the host):

```bash
./server_qnx_sensors 120     # run 120 s (0 = until Ctrl-C)
```

From the host, run the companion client (`tools/host_client.c`,
built natively against the amalgamation by
`scripts/build-host-client.sh`):

```bash
./host_client opc.tcp://127.0.0.1:4840
```

**Checkpoint — the host client reports ALL PASS, exit 0** (verified
output, 2026-11-13, QNX 8.0 under QEMU):

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
the first-order lag, not a jump. That is the port, running on QNX,
serving a real client.

> **A second `run_iterate` gotcha.** `UA_Server_run_iterate`
> returns the **milliseconds until the next callback** (`UA_UInt16`),
> not a status code. The first demo loop treated any non-zero return
> as an error and killed the server seconds after start (observed
> `ITERATE-ERROR 0x18e` = 398 ms) — which also made the host client
> time out mid-handshake. The fix: ignore the return value, as
> upstream does.

> **What was deliberately not run:** upstream's `check_*` unit-test
> suite needs the `libcheck` framework, which is not available for
> QNX 8.0. The tests above cover the same API surface (types,
> eventloop, server/client) without it — say so plainly in your own
> port's README rather than claiming the upstream suite ran.

---

## Step 9 — Package the port like you mean it

Duration: 5:00

A port that only exists on your laptop is not a port. Package it the
way upstream and the QNX community expect:

```
open62541-qnx/
  patches/            G1–G4 as clean diffs vs upstream v1.5.1
  cmake/toolchain-qnx-x86_64.cmake
  scripts/build.sh            clone -> patch -> build lib + amalgamation
  scripts/build-target.sh     demo server + on-target tests (qcc)
  scripts/build-host-client.sh
  examples/server_qnx_sensors.c
  tests-on-target/            no libcheck dependency
  tools/host_client.c
  README.md                   version, target, toolchain, verified results
  LICENSE                     MPL-2.0 (same as upstream)
```

Rules that matter:

* **New repository, not a fork.** Reference upstream by URL; the
  repo contains only the porting work.
* **Patches apply cleanly, in order, to a pristine upstream tag.**
  Verify with `git apply --check` — a patch that only applies to
  your dirty tree is not a patch.
* **README states the version, the target, and the toolchain, and
  shows verified on-target results** — a README that only
  name-drops QNX is explicitly insufficient for the hackathon's
  OSS Ports category.
* **Offer the fixes back.** The G2+G3+G4 fixes were offered upstream
  as [PR #8717](https://github.com/open62541/open62541/pull/8717)
  ("fix(arch): build the POSIX architecture on QNX 8.0 (1.5
  backport)", base `1.5`). G1 was already on 1.5 via upstream
  `2c9beadfc`. Say plainly in the PR and the README that it is a
  backport to the 1.5 maintenance line — upstream `master` fixed the
  equivalent build on 2026-11-11 — and that the entry's value is the
  verified port plus on-target proof, not a first-fix claim.

**Checkpoint — fresh-clone verification:** clone your own repo on a
clean machine (or a clean directory), follow the README alone, and
confirm `scripts/build.sh` produces `libopen62541.a` with no manual
steps and no shims. If it needs a step the README does not mention,
the README is wrong — fix the README, not the process.

---

## Going further — encrypted OPC UA on QNX (optional)

Duration: 15:00

The demo ships security None, but the encrypted configuration is
real and verified on target — it is a separate build, not a claim
about the demo:

```bash
scripts/build-encryption.sh
# -> build-encryption/install/lib/libopen62541.a
# -> build-encryption/target/test_encryption
```

* **OpenSSL comes from the SDP itself** — SDP 8.0 bundles OpenSSL
  3.0.20 for the target (headers in
  `$QNX_TARGET/usr/include/openssl`, libraries in
  `$QNX_TARGET/x86_64/usr/lib/`). No dashboard download is needed,
  and the target image already ships `libcrypto.so.3` /
  `libssl.so.3` in the IFS.
* **Build quirk:** CMake's `FindOpenSSL` finds the headers and
  version from the sysroot but **not** the libraries (they live in
  the arch-specific `x86_64/usr/lib`, which `FindOpenSSL` does not
  search) — configure fails with `missing:
  OPENSSL_CRYPTO_LIBRARY` unless both libraries are passed
  explicitly; the script does that.
* `tests-on-target/test_encryption.c` generates two self-signed
  certificates in-process (RSA-2048), starts a **secure-only**
  server on port 4841 (`Basic256Sha256`, `SignAndEncrypt`, no None
  endpoint), and connects a client the same way; it reads `TestValue
  = 42` back over the encrypted channel. Verified on target:
  `encryption: ALL PASS`. Runtime is ~90 s under QEMU/TCG, mostly
  the two RSA-2048 key generations.
* **Two gotchas** (both in the test's comments):
  `UA_ServerConfig_setDefaultWithSecureSecurityPolicies` resets the
  config first, so the server's `ApplicationUri` must be set
  **after** that call — setting it before is silently wiped. And
  the client's `ApplicationUri` must match the URI in its
  certificate, or the server rejects the session with
  `BadCertificateInvalid` at `CreateSession`.

---

## What you built

You took a library that did not compile for QNX 8.0, found the four
defects that stopped it (a glibc-only header, a missing system
header, two phantom link libraries, a split socket library), fixed
them in ~10 lines plus a toolchain file, and proved the result on a
real QNX target under QEMU — types, eventloop, handshake, a demo
server serving a host client, and an encrypted round-trip.

The same loop works for the next library: pick the candidate, check
the dashboard, read the build system, fix the defects the build
surfaces (in order — never guess ahead of the error), prove it on
target, and offer the fixes upstream. The companion repo for this
codelab is
[buahrasa01-hub/open62541-qnx](https://github.com/buahrasa01-hub/open62541-qnx);
the upstream project is
[open62541/open62541](https://github.com/open62541/open62541), and
the fixes are at
[PR #8717](https://github.com/open62541/open62541/pull/8717).
