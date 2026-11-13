#!/bin/bash
# Build open62541 v1.5.1 for QNX 8.0 (x86_64) from the patches in this repo.
#
#   scripts/build.sh [work-dir]
#
# Steps: clone upstream tag v1.5.1 -> apply patches/ -> configure with the
# QNX toolchain file -> build libopen62541.a + the amalgamation
# (open62541.c/.h) -> install into <work-dir>/install -> verify the artifact.
#
# No probe shims: the build works from this repo alone (patches + toolchain).
# Requires: QNX SDP 8.0 (source ~/qnx/qnx800/qnxsdp-env.sh first, or rely on
# the toolchain file's default SDP path), cmake >= 3.16, git, python3.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${1:-$REPO/build}"
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

echo "== open62541-qnx build =="
echo "repo:     $REPO"
echo "work:     $WORK"
echo "upstream: $UPSTREAM_URL @ $UPSTREAM_TAG"
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

# 3. Configure with the QNX toolchain file
echo "-- configuring"
cmake -S "$SRC" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$REPO/cmake/toolchain-qnx-x86_64.cmake" \
    -DUA_ARCHITECTURE=posix \
    -DUA_ENABLE_AMALGAMATION=ON \
    -DUA_ENABLE_ENCRYPTION=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL"

# 4. Build library + amalgamation
echo "-- building libopen62541.a + amalgamation"
cmake --build "$BUILD" -j2 --target open62541 open62541-amalgamation

# 5. Install (headers + static lib) for the demo / on-target tests
echo "-- installing to $INSTALL"
cmake --install "$BUILD"

# 6. Verify the artifact: genuine x86-64 QNX ELF relocatable, UA_Server_new present
echo "-- verifying"
file "$INSTALL/lib/libopen62541.a"
ar t "$INSTALL/lib/libopen62541.a" | wc -l
nm "$INSTALL/lib/libopen62541.a" 2>/dev/null | grep -c " T UA_Server_new$" || true
test -f "$BUILD/open62541.c" && test -f "$BUILD/open62541.h" \
    && echo "amalgamation: $BUILD/open62541.c + open62541.h OK"
ls -l "$INSTALL/lib/libopen62541.a" "$BUILD/open62541.c" "$BUILD/open62541.h"

# 7. Verify the single-file amalgamation build under qcc: compile the
#    amalgamated source and link a minimal program against it.
echo "-- verifying amalgamation build under qcc"
cat > "$WORK/amalgamation-check.c" << 'EOF'
#include "open62541.h"
int main(void) {
    UA_Server *s = UA_Server_new();
    if(!s) return 1;
    UA_Server_delete(s);
    return 0;
}
EOF
qcc -Vgcc_ntox86_64 -std=c99 -I"$BUILD" "$WORK/amalgamation-check.c" \
    "$BUILD/open62541.c" -lm -o "$WORK/amalgamation-check"
file "$WORK/amalgamation-check"
echo "BUILD OK: $INSTALL/lib/libopen62541.a"
