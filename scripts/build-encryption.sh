#!/bin/bash
# Build open62541 v1.5.1 for QNX 8.0 (x86_64) WITH OpenSSL encryption.
#
#   scripts/build-encryption.sh [work-dir]
#
# Same steps as scripts/build.sh, except UA_ENABLE_ENCRYPTION=OPENSSL, and
# it also builds tests-on-target/test_encryption.c (the on-target encrypted
# client<->server round-trip test) against the encrypted library.
#
# Verified 2026-11-13: the encrypted build works on target — Basic256Sha256
# SignAndEncrypt, secure-only server (no None endpoint), TestValue read back
# over the encrypted channel (see tests-on-target/test_encryption.c).
#
# OpenSSL source: the SDP itself bundles OpenSSL 3.0.20 for the target
# (headers in $QNX_TARGET/usr/include/openssl, libs in
# $QNX_TARGET/x86_64/usr/lib/libcrypto.so / libssl.so). No dashboard
# download is needed. The target image already ships libcrypto.so.3 and
# libssl.so.3 in the IFS, so the test runs without extra libraries.
#
# QUIRK (CMake FindOpenSSL under cross-compile): FindOpenSSL finds the
# headers and the version from the sysroot, but NOT the libraries — they
# live in the arch-specific $QNX_TARGET/x86_64/usr/lib, which FindOpenSSL
# does not search. Pass both libraries explicitly (below); without them
# configure fails with "missing: OPENSSL_CRYPTO_LIBRARY".
#
# Requires: QNX SDP 8.0 (source ~/qnx/qnx800/qnxsdp-env.sh first, or rely on
# the toolchain file's default SDP path), cmake >= 3.16, git, python3.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${1:-$REPO/build-encryption}"
UPSTREAM_URL="${UPSTREAM_URL:-https://github.com/open62541/open62541.git}"
UPSTREAM_TAG="${UPSTREAM_TAG:-v1.5.1}"

SRC="$WORK/upstream"
BUILD="$WORK/build"
INSTALL="$WORK/install"

# SDP environment (qcc on PATH, QNX_HOST/QNX_TARGET set)
if [ -f "$HOME/qnx/qnx800/qnxsdp-env.sh" ]; then
    # shellcheck disable=SC1091
    source "$HOME/qnx/qnx800/qnxsdp-env.sh"
fi
command -v qcc >/dev/null || { echo "ERROR: qcc not found (source ~/qnx/qnx800/qnxsdp-env.sh)"; exit 1; }
command -v cmake >/dev/null || { echo "ERROR: cmake not found"; exit 1; }
[ -n "${QNX_TARGET:-}" ] || { echo "ERROR: QNX_TARGET not set"; exit 1; }

echo "== open62541-qnx encryption build =="
echo "repo:     $REPO"
echo "work:     $WORK"
echo "upstream: $UPSTREAM_URL @ $UPSTREAM_TAG"
echo "openssl:  $QNX_TARGET/x86_64/usr/lib/libcrypto.so (SDP-bundled)"
qcc -Vgcc_ntox86_64 -dumpversion 2>/dev/null || true

# 1. Upstream source at the pinned tag (fresh clone, or reset a reused one)
if [ -d "$SRC/.git" ]; then
    echo "-- reusing $SRC (reset to $UPSTREAM_TAG)"
    git -C "$SRC" fetch --quiet --tags origin 2>/dev/null || true
    git -C "$SRC" checkout --quiet "$UPSTREAM_TAG"
    git -C "$SRC" clean -fdx --quiet
    git -C "$SRC" checkout --quiet .
else
    rm -rf "$SRC"
    git clone --quiet --depth 1 --branch "$UPSTREAM_TAG" "$UPSTREAM_URL" "$SRC"
fi
git -C "$SRC" rev-parse HEAD

# 2. Apply the QNX patches (must apply cleanly, in order)
echo "-- applying patches"
for p in "$REPO"/patches/*.patch; do
    echo "   $(basename "$p")"
    git -C "$SRC" apply --check "$p"
done
for p in "$REPO"/patches/*.patch; do
    git -C "$SRC" apply "$p"
done
git -C "$SRC" diff --stat

# 3. Configure with the QNX toolchain file + OpenSSL encryption
echo "-- configuring (UA_ENABLE_ENCRYPTION=OPENSSL)"
cmake -S "$SRC" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/toolchain-qnx-x86_64.cmake" \
    -DUA_ARCHITECTURE=posix \
    -DUA_ENABLE_AMALGAMATION=ON \
    -DUA_ENABLE_ENCRYPTION=OPENSSL \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL" \
    -DOPENSSL_CRYPTO_LIBRARY="$QNX_TARGET/x86_64/usr/lib/libcrypto.so" \
    -DOPENSSL_SSL_LIBRARY="$QNX_TARGET/x86_64/usr/lib/libssl.so"

# 4. Build library + amalgamation
echo "-- building libopen62541.a + amalgamation"
cmake --build "$BUILD" -j2 --target open62541 open62541-amalgamation

# 5. Install (headers + static lib) for the encryption test
echo "-- installing to $INSTALL"
cmake --install "$BUILD"

# 6. Verify the artifact: genuine x86-64 QNX ELF relocatable, UA_Server_new
#    present, and the OpenSSL security policy compiled in.
echo "-- verifying"
file "$INSTALL/lib/libopen62541.a"
ar t "$INSTALL/lib/libopen62541.a" | wc -l
nm "$INSTALL/lib/libopen62541.a" 2>/dev/null | grep -c " T UA_Server_new$" || true
nm "$INSTALL/lib/libopen62541.a" 2>/dev/null | grep -c "securitypolicy_basic256sha256" || true
test -f "$BUILD/open62541.c" && test -f "$BUILD/open62541.h" \
    && echo "amalgamation: $BUILD/open62541.c + open62541.h OK"
ls -l "$INSTALL/lib/libopen62541.a" "$BUILD/open62541.c" "$BUILD/open62541.h"

# 7. Build the on-target encryption test against the encrypted library.
#    Links -lcrypto -lssl: the target image already ships both in the IFS.
echo "-- building test_encryption"
mkdir -p "$WORK/target"
qcc -Vgcc_ntox86_64 -std=c99 -I"$INSTALL/include" \
    "$REPO/tests-on-target/test_encryption.c" \
    -L"$INSTALL/lib" -lopen62541 -lcrypto -lssl -lm -lsocket \
    -o "$WORK/target/test_encryption"
file "$WORK/target/test_encryption"
echo "ENCRYPTION BUILD OK: $INSTALL/lib/libopen62541.a + $WORK/target/test_encryption"
echo "Run on target (scp it in, then ./test_encryption — takes ~90 s under"
echo "QEMU/TCG, mostly two RSA-2048 key generations)."
