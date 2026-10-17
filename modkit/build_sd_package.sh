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
# It also derives an Android-Auto-only JAR (Update/aa_only/<name>_aa.jar,
# without the CarPlay classes) that install.sh prefers on Audi unless
# enable_carplay.txt is present. The Audi stand-in jar for the Porsche-only
# StorageMountHandler class (Update/audi/, built by modkit/stub/build_stub.sh)
# is tracked under modkit/Mods and copied in with the overlay.
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
# For the private P2873 diagnostic card with saved stock GAL, checksums and
# verbose recovery instructions, use build_audi_sd.py instead. Neither
# packager builds or includes the locally changed lifecycle/native sources.

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

# Derive an Android-Auto-only JAR next to each release JAR. install.sh uses it
# on Audi unless enable_carplay.txt is present. The carplay package and the
# androidauto2 package have no cross-references (verified with javap/grep on
# v0034), so dropping the CarPlay classes needs no recompilation. Every
# androidauto2 class stays, AndroidAuto2EventListener included: its
# constructor is the only place that subscribes the DSI attributes, and the
# mod's copy subscribes {3,5,6,7,8} where the stock Audi class subscribes
# {3,5,6}; 7 and 8 are the next-turn event and the distance to the next turn,
# without which the mod never receives a manoeuvre (no BAP_MANEUVER lines).
# An earlier build dropped it as a "pure stock copy" because its method
# signatures match stock; the difference is in the constructor body.
python3 - "$PKG/Mods/ClusterIntegration/Update" <<'PY'
import os, sys, zipfile
upd = sys.argv[1]
DROP_PREFIX = "de/audi/app/terminalmode/smartphone/carplay/"
DROP_EXACT = set()
out_dir = os.path.join(upd, "aa_only")
os.makedirs(out_dir, exist_ok=True)
# A tracked aa_only jar may have been overlaid from modkit/Mods; drop any
# that does not belong to a release JAR present in this package, so the
# installer's "newest aa jar" choice is never ambiguous.
release = {n[:-4] for n in os.listdir(upd) if n.startswith("ClusterIntegration_") and n.endswith(".jar")}
for stale in os.listdir(out_dir):
    if stale.endswith("_aa.jar") and stale[:-7] not in release:
        os.remove(os.path.join(out_dir, stale))
        print(f"  drop:    Mods/ClusterIntegration/Update/aa_only/{stale} (no matching release JAR)")
for name in sorted(os.listdir(upd)):
    if not (name.startswith("ClusterIntegration_") and name.endswith(".jar")):
        continue
    src = os.path.join(upd, name)
    dst = os.path.join(out_dir, name[:-4] + "_aa.jar")
    kept = dropped = 0
    with zipfile.ZipFile(src) as zin, zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as zout:
        for info in zin.infolist():
            if info.filename.startswith(DROP_PREFIX) or info.filename in DROP_EXACT:
                dropped += 1
                continue
            zout.writestr(info, zin.read(info))
            kept += 1
    print(f"  derive:  Mods/ClusterIntegration/Update/aa_only/{os.path.basename(dst)} ({kept} entries kept, {dropped} CarPlay entries dropped)")
PY

rm -f "$OUT_ABS"
( cd "$PKG" && zip -q -r -X "$OUT_ABS" . )
echo "output:  $OUT ($changed file(s) changed vs source, $(du -h "$OUT_ABS" | cut -f1))"
