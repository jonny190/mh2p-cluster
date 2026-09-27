#!/usr/bin/env bash
#
# build_sd_package.sh — rebuild a flashable MH2p SD ModKit package from an
# existing release zip, overlaying the ModKit mod scripts tracked in this
# repository (modkit/Mods/**) on top of the shipped binaries.
#
# The release zips under builds/ carry the signed LawPaul ModKit (Data/, Meta/)
# plus the compiled mod (JAR, cluster daemon, gal_cluster.so, dio_cluster.so,
# cluster_config.json). Only Mods/ is unsigned and unchecksummed, so this is
# the safe place to change install behaviour without a QNX toolchain.
#
# Usage:
#   modkit/build_sd_package.sh <source-release.zip> <output.zip>
#
# Example:
#   modkit/build_sd_package.sh \
#       builds/ClusterIntegration_v0034_beta2_candidate_90d0b76.zip \
#       builds/ClusterIntegration_v0034_beta2_candidate_90d0b76_audi.zip
#
# Requires: bash, python3, zip. Runs on Linux/macOS/WSL (not on the head unit).

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <source-release.zip> <output.zip>" >&2
    exit 1
fi

SRC="$1"
OUT="$2"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OVERLAY="$HERE/Mods"

[[ -f "$SRC" ]] || { echo "error: source zip not found: $SRC" >&2; exit 1; }
[[ -d "$OVERLAY" ]] || { echo "error: overlay dir not found: $OVERLAY" >&2; exit 1; }
command -v python3 >/dev/null || { echo "error: python3 is required" >&2; exit 1; }
command -v zip >/dev/null || { echo "error: zip is required" >&2; exit 1; }

case "$OUT" in
    /*) OUT_ABS="$OUT" ;;
    *)  OUT_ABS="$(pwd)/$OUT" ;;
esac

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
PKG="$WORK/pkg"
mkdir -p "$PKG"

# Extract with Python: the upstream zips were built on Windows and store
# backslash path separators, which unzip(1) refuses to normalise.
python3 - "$SRC" "$PKG" <<'PY'
import os, sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(src) as zf:
    for info in zf.infolist():
        name = info.filename.replace("\\", "/")
        if name.endswith("/") or not name:
            continue
        if name.startswith("/") or ".." in name.split("/"):
            raise SystemExit(f"refusing unsafe zip entry: {info.filename}")
        target = os.path.join(dst, name)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as out:
            out.write(zf.read(info))
PY

echo "source:  $SRC"
echo "overlay: $OVERLAY"
changed=0
while IFS= read -r -d '' f; do
    rel="${f#"$OVERLAY"/}"
    dest="$PKG/Mods/$rel"
    mkdir -p "$(dirname "$dest")"
    if [[ -f "$dest" ]] && cmp -s "$f" "$dest"; then
        echo "  same:    Mods/$rel"
    else
        if [[ -f "$dest" ]]; then echo "  replace: Mods/$rel"; else echo "  add:     Mods/$rel"; fi
        changed=$((changed + 1))
    fi
    cp "$f" "$dest"
    chmod 755 "$dest"
done < <(find "$OVERLAY" -type f -print0 | sort -z)

rm -f "$OUT_ABS"
( cd "$PKG" && zip -q -r -X "$OUT_ABS" . )
echo "output:  $OUT ($changed file(s) changed vs source, $(du -h "$OUT_ABS" | cut -f1))"
