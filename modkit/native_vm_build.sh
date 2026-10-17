#!/bin/bash
# Local build recipe for Ubuntu 12.04; no modern Python dependency.
set -eu
TASK_ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$TASK_ROOT"
: "${QNX_HOST:?Source your SDK qnx660-env.sh first}"
: "${QNX_TARGET:?Source your SDK qnx660-env.sh first}"
command -v qcc >/dev/null || { echo 'QNX qcc compiler missing' >&2; exit 1; }
SDK_ROOT="$(dirname "$(dirname "$QNX_TARGET")")"
[[ -f "$SDK_ROOT/qnx660-env.sh" ]] || { echo 'Expected QNX SDP 6.6 environment' >&2; exit 1; }
python native_vm_receipt.py check
BUILD_SHA=$(python -c 'import json; print(json.load(open("source-manifest.json"))["source_fingerprint"])')
BUILD_DEFINE="-DMIB2P_BUILD_FINGERPRINT=\"$BUILD_SHA\""
mkdir -p out
set -x
qcc -Vgcc_ntoarmv7le -O2 -g -Wall -std=gnu99 "$BUILD_DEFINE" -shared -fPIC -o out/gal_cluster.so src/gal_cluster.c -lscreen -lc
qcc -Vgcc_ntoarmv7le -O2 -g -Wall -std=gnu99 "$BUILD_DEFINE" -o out/cluster src/cluster.c -lscreen -lEGL -lz -lc
set +x
python native_vm_receipt.py receipt
echo 'Native outputs and native-build.json are in out/. Copy all three back to Windows for import-vm.'
