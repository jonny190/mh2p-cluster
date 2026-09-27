#!/bin/ksh
#
# Copyright (c) 2026 fifthBro
# https://fifthbro.github.io
#
# Licensed under CC BY-NC-SA 4.0
# https://creativecommons.org/licenses/by-nc-sa/4.0/
# NOT FOR COMMERCIAL USE
#
# uninstall.sh 
#
# Idempotent uninstaller. Re-runs are safe.
# - Restores original gal binary: removes wrapper at /mnt/app/eso/bin/apps/gal
#   and moves gal.real back to gal.
# - Same for dio_manager.
# - Backs up and removes JAR + native binaries from their install dirs.

set -u

export MOD_PATH="${modPath:-${MOD_PATH:-}}"
[[ -z "$MOD_PATH" ]] && { print -u2 "ERROR: MOD_PATH not set"; exit 1; }

[[ ! -e /mnt/app ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
mount -uw /mnt/app/

export RELEASE_VERSION=`/mnt/app/armle/usr/bin/pc b:46924065:401 | cut -c 61- | sed ':a;N;$!ba;s/\n//g' | sed -e 's/\.//g' | sed -e 's/ //g'`
export OEM="$(echo $RELEASE_VERSION | cut -d'_' -f3 | cut -b -2)"

# No OEM gate on uninstall: every step below only restores or removes files
# this mod created (gal.real / dio_manager.real, our JAR, our cluster dir),
# so it is safe on Porsche, Audi, or any unit where the mod was forced in.
print "Head unit:         release=$RELEASE_VERSION oem=$OEM"

JAR_DIR=/mnt/app/eso/hmi/lsd/jars
CLUSTER_DIR=/mnt/app/eso/bin/apps/cluster
APPS_DIR=/mnt/app/eso/bin/apps

BACKUP_DIR="$MOD_PATH/Backup"
mkdir -p "$BACKUP_DIR" || { print -u2 "ERROR: cannot mkdir $BACKUP_DIR"; exit 2; }

ts() { date +"%Y%m%d_%H%M%S"; }

print "Slaying running processes before uninstall..."
for proc in cluster gal gal.real dio_manager dio_manager.real; do
    slay -f "$proc" 2>/dev/null
    if [[ $? -eq 0 ]]; then
        print "slay:              $proc"
    fi
done
print ""

# Reverse of swap_binary_for_wrapper. If NAME.real exists, remove the
# wrapper at NAME and move NAME.real -> NAME. If NAME.real doesn't exist,
# nothing was swapped �� skip.
restore_wrapped_binary() {
    name="$1"
    dst_active="$APPS_DIR/$name"
    dst_real="$APPS_DIR/${name}.real"

    if [[ ! -f "$dst_real" ]]; then
        print "skip restore:      $name (no $dst_real present)"
        return 0
    fi
    if [[ -f "$dst_active" ]]; then
        bk="$BACKUP_DIR/${name}.wrapper.removed.$(ts)"
        cp -p "$dst_active" "$bk" 2>/dev/null && print "backup wrapper:    $dst_active -> $bk"
        rm -f "$dst_active"
    fi
    mv "$dst_real" "$dst_active" || { print -u2 "ERROR: mv $name.real -> $name failed"; return 2; }
    print "restore original:  $dst_real -> $dst_active"
}

backup_and_remove() {
    f="$1"
    if [[ ! -f "$f" ]]; then
        print "skip absent:       $f"
        return 0
    fi
    bk="$BACKUP_DIR/${f##*/}.removed.$(ts)"
    cp -p "$f" "$bk" || { print -u2 "ERROR: backup failed: $f"; return 2; }
    rm -f "$f"
    print "remove:            $f -> $bk"
}

# need our .so files until processes restart, but removing them after the
# restore is fine because next gal/dio_manager spawn won't use them). ──
restore_wrapped_binary gal
restore_wrapped_binary dio_manager

backup_and_remove "$CLUSTER_DIR/cluster"
backup_and_remove "$CLUSTER_DIR/gal_cluster.so"
backup_and_remove "$CLUSTER_DIR/dio_cluster.so"
backup_and_remove "$CLUSTER_DIR/cluster_config.json"

typeset j
for j in "$JAR_DIR"/ClusterIntegration_* "$JAR_DIR"/AndroidAutoCluster_*; do
    [[ -f "$j" ]] && backup_and_remove "$j"
done

if [[ -d "$CLUSTER_DIR" ]] && [[ -z "$(ls -A "$CLUSTER_DIR" 2>/dev/null)" ]]; then
    rmdir "$CLUSTER_DIR" 2>/dev/null && print "rmdir empty:       $CLUSTER_DIR"
fi

sync
print ""
print "Done."

