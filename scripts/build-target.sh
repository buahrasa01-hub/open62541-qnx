#!/bin/bash
# Build the demo server + on-target tests with qcc against a library built
# by scripts/build.sh.
#
#   scripts/build-target.sh [install-prefix] [output-dir]
#
# Defaults: install-prefix = <repo>/build/install, output = <repo>/build/target
# Requires: QNX SDP 8.0 (source ~/qnx/qnx800/qnxsdp-env.sh first).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALL="${1:-$REPO/build/install}"
OUT="${2:-$REPO/build/target}"

if [ -f "$HOME/qnx/qnx800/qnxsdp-env.sh" ]; then
    # shellcheck disable=SC1091
    source "$HOME/qnx/qnx800/qnxsdp-env.sh"
fi
command -v qcc >/dev/null || { echo "ERROR: qcc not found"; exit 1; }
[ -f "$INSTALL/lib/libopen62541.a" ] || {
    echo "ERROR: $INSTALL/lib/libopen62541.a not found — run scripts/build.sh first"
    exit 1
}

mkdir -p "$OUT"
QCC="qcc -Vgcc_ntox86_64 -std=c99"
echo "== building QNX target binaries into $OUT =="

echo "-- server_qnx_sensors"
$QCC -I"$INSTALL/include" "$REPO/examples/server_qnx_sensors.c" \
    -L"$INSTALL/lib" -lopen62541 -lm -o "$OUT/server_qnx_sensors"

echo "-- test_types_roundtrip"
$QCC -I"$INSTALL/include" "$REPO/tests-on-target/test_types_roundtrip.c" \
    -L"$INSTALL/lib" -lopen62541 -lm -o "$OUT/test_types_roundtrip"

echo "-- test_eventloop"
$QCC -I"$INSTALL/include" "$REPO/tests-on-target/test_eventloop.c" \
    -L"$INSTALL/lib" -lopen62541 -lm -o "$OUT/test_eventloop"

echo "-- test_server_client"
$QCC -pthread -I"$INSTALL/include" \
    "$REPO/tests-on-target/test_server_client.c" \
    -L"$INSTALL/lib" -lopen62541 -lm -o "$OUT/test_server_client"

echo "-- verifying artifacts"
for f in server_qnx_sensors test_types_roundtrip test_eventloop test_server_client; do
    file "$OUT/$f"
done
echo "TARGET BUILD OK: $OUT"
