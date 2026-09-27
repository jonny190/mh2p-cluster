#!/usr/bin/env bash
#
# check_hmi_signatures.sh — compare the constructor and method signatures of
# the classes this mod SHADOWS against the stock HMI jars of a head unit.
#
# The mod JAR replaces de.audi.app.terminalmode.smartphone.androidauto2.
# AndroidAuto2Subsystem (and, in the full JAR, ...carplay.CarPlayDSIManager)
# by being placed next to the stock jars. The stock factory constructs those
# classes with a fixed argument list, so if the unit's own class has a
# different constructor the mod class fails to link and Android Auto or
# CarPlay stop working. This script makes that comparison on a PC before
# going live.
#
# Where to get the stock jars: a diagnostic-mode install (diag.txt) copies
# every jar under /mnt/app/eso/hmi/lsd/jars that contains one of the
# shadowed classes to Mods/ClusterIntegration/Update/Backup/hmi_reference/
# on the SD card. Over SSH you can also copy them from that directory.
#
# Usage:
#   modkit/check_hmi_signatures.sh <mod.jar> <dir-with-stock-jars>
#
# Requires: javap (any JDK), unzip. Exit code 0 = every shadowed class found
# in the stock jars has identical constructor/method signatures, 1 = mismatch
# or a shadowed class was not found in the stock jars.

set -euo pipefail
if [[ $# -ne 2 ]]; then echo "usage: $0 <mod.jar> <dir-with-stock-jars>" >&2; exit 2; fi
MOD_JAR="$1"; REF_DIR="$2"
command -v javap >/dev/null || { echo "error: javap not found (install a JDK)" >&2; exit 2; }
command -v unzip >/dev/null || { echo "error: unzip not found" >&2; exit 2; }
[[ -f "$MOD_JAR" ]] || { echo "error: mod jar not found: $MOD_JAR" >&2; exit 2; }
[[ -d "$REF_DIR" ]] || { echo "error: reference dir not found: $REF_DIR" >&2; exit 2; }

SHADOWED=(
  de/audi/app/terminalmode/smartphone/androidauto2/AndroidAuto2Subsystem
  de/audi/app/terminalmode/smartphone/androidauto2/AndroidAuto2EventListener
  de/audi/app/terminalmode/smartphone/carplay/CarPlayDSIManager
)

# javap output for one class from one jar: public/protected/package members,
# signatures only, sorted so ordering differences do not count.
sigs() { # jar class
  javap -p -cp "$1" "${2//\//.}" 2>/dev/null \
    | grep -vE '^(Compiled from|\s*$|[a-z ]*class |\})' \
    | sed -e 's/^ *//' -e 's/ *$//' | sort
}

rc=0
mod_entries="$(unzip -Z1 "$MOD_JAR")"
for cls in "${SHADOWED[@]}"; do
  if ! grep -qx "$cls.class" <<<"$mod_entries"; then
    echo "skip     $cls (not in $MOD_JAR)"
    continue
  fi
  ref_jar=""
  while IFS= read -r -d '' j; do
    if unzip -Z1 "$j" 2>/dev/null | grep -qx "$cls.class"; then ref_jar="$j"; break; fi
  done < <(find "$REF_DIR" -name '*.jar' -print0 | sort -z)
  if [[ -z "$ref_jar" ]]; then
    echo "MISSING  $cls not found in any jar under $REF_DIR"
    rc=1; continue
  fi
  mod_sigs="$(sigs "$MOD_JAR" "$cls")"
  ref_sigs="$(sigs "$ref_jar" "$cls")"
  # Constructors are what the stock factory calls; report them separately.
  short="${cls##*/}"
  mod_ctors="$(grep -F "$short(" <<<"$mod_sigs" || true)"
  ref_ctors="$(grep -F "$short(" <<<"$ref_sigs" || true)"
  if [[ "$mod_ctors" == "$ref_ctors" ]]; then
    echo "MATCH    $cls constructors identical to ${ref_jar##*/}"
  else
    echo "MISMATCH $cls constructors differ from ${ref_jar##*/}"
    diff <(echo "$mod_ctors") <(echo "$ref_ctors") | sed 's/^/           /' || true
    rc=1
  fi
  if [[ "$mod_sigs" != "$ref_sigs" ]]; then
    n_only_ref=$(comm -13 <(echo "$mod_sigs") <(echo "$ref_sigs") | grep -c . || true)
    n_only_mod=$(comm -23 <(echo "$mod_sigs") <(echo "$ref_sigs") | grep -c . || true)
    echo "           members only in stock class: $n_only_ref, only in mod class: $n_only_mod (mod-added members are expected; stock-only members mean the Audi class has API the mod copy lacks)"
    comm -13 <(echo "$mod_sigs") <(echo "$ref_sigs") | sed 's/^/           stock-only: /'
  fi
done
exit $rc
