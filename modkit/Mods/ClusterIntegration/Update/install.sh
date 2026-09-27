#!/bin/ksh
#
# Copyright (c) 2026 fifthBro
# https://fifthbro.github.io
#
# Licensed under CC BY-NC-SA 4.0
# https://creativecommons.org/licenses/by-nc-sa/4.0/
# NOT FOR COMMERCIAL USE
#
# install.sh 
#
# Idempotent installer. Re-runs are safe.
# - Supported head units: Porsche PCM5 (OEM=PO, firmware 26xx/28xx, tested)
#   and Audi MH2p (OEM=AU, experimental). Other OEMs abort unless
#   Mods/ClusterIntegration/force_install.txt exists on the SD card.
# - Copies JAR, cluster, gal_cluster.so, dio_cluster.so to their target dirs.
# - Skips copies when the target file is byte-identical to the source.
# - Backs up any existing target before overwriting (timestamped, into
#   $MOD_PATH/Backup/).
# - Replaces /mnt/app/eso/bin/apps/gal with a wrapper script that sets
#   LD_PRELOAD=gal_cluster.so before execing the real gal binary.
#   The original binary is preserved at /mnt/app/eso/bin/apps/gal.real so
#   uninstall.sh can restore it.
# - Same for dio_manager.
#

set -u

export MOD_PATH="${modPath:-${MOD_PATH:-}}"
[[ -z "$MOD_PATH" ]] && { print -u2 "ERROR: MOD_PATH not set"; exit 1; }

[[ ! -e /mnt/app ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
mount -uw /mnt/app/

# ex: MH2p_US_PO416_P2870 (Porsche), MH2p_ER_AUG36_P0xxx (Audi)
export RELEASE_VERSION=`/mnt/app/armle/usr/bin/pc b:46924065:401 | cut -c 61- | sed ':a;N;$!ba;s/\n//g' | sed -e 's/\.//g' | sed -e 's/ //g'`
# AS, CN, ER, US, ...
export REGION="$(echo $RELEASE_VERSION | cut -d'_' -f2)"
# VW, AU, PO, LB, ...
export OEM="$(echo $RELEASE_VERSION | cut -d'_' -f3 | cut -b -2)"
# 416, 636, G33, G35, G36, ...
export TYPE="$(echo $RELEASE_VERSION | cut -d'_' -f3 | cut -b 3-)"
# 9830, 2870, ...
export SOFTWARE_VERSION="$(echo $RELEASE_VERSION | cut -d'_' -f4 | cut -b 2-)"

# Override marker. Create Mods/ClusterIntegration/force_install.txt on the
# SD card (next to the Update/ folder) to skip the OEM / firmware gate.
# Only for people who know their head unit is MH2p and accept the risk.
MOD_ROOT="${MOD_PATH%/*}"
FORCE_INSTALL=0
[[ -e "$MOD_ROOT/force_install.txt" ]] && FORCE_INSTALL=1

print "Head unit:         release=$RELEASE_VERSION oem=$OEM type=$TYPE region=$REGION sw=$SOFTWARE_VERSION force=$FORCE_INSTALL"

# Supported head units:
#   PO  Porsche PCM5 (MH2P) firmware 26xx / 28xx     - tested
#   AU  Audi MH2p (e.g. e-tron GE, A6 C8, Q8)         - EXPERIMENTAL, untested
# Anything else aborts unless force_install.txt is present.
case "$OEM" in
    PO)
        if [[ "$SOFTWARE_VERSION" != 26?? && "$SOFTWARE_VERSION" != 28?? ]]; then
            if [[ $FORCE_INSTALL -eq 1 ]]; then
                print "WARNING: Porsche firmware $RELEASE_VERSION outside tested range (26xx / 28xx), continuing because force_install.txt is present."
            else
                print "Firmware $RELEASE_VERSION not in supported range (26xx / 28xx). Aborting."
                print "To override, create Mods/ClusterIntegration/force_install.txt on the SD card."
                exit 0
            fi
        else
            print "Porsche firmware $RELEASE_VERSION OK, installing..."
        fi
        ;;
    AU)
        print "Audi MH2p head unit $RELEASE_VERSION detected. Audi support is EXPERIMENTAL."
        print "No Audi firmware range has been validated yet, so the Porsche 26xx/28xx check is skipped."
        print "If Android Auto or CarPlay stop working: put uninstall.txt in Mods/ClusterIntegration/ and re-run the ModKit."
        ;;
    *)
        if [[ $FORCE_INSTALL -eq 1 ]]; then
            print "WARNING: unsupported head unit (OEM='$OEM', release='$RELEASE_VERSION'), continuing because force_install.txt is present."
        else
            print "Unsupported head unit (OEM='$OEM', release='$RELEASE_VERSION'). Aborting."
            print "Supported: Porsche (PO) firmware 26xx/28xx, Audi (AU, experimental)."
            print "To override, create Mods/ClusterIntegration/force_install.txt on the SD card."
            exit 0
        fi
        ;;
esac

JAR_DIR=/mnt/app/eso/hmi/lsd/jars
CLUSTER_DIR=/mnt/app/eso/bin/apps/cluster
APPS_DIR=/mnt/app/eso/bin/apps

# Preflight: refuse to touch anything if the MH2p layout we expect is not
# there. Protects against running on a head unit whose /mnt/app differs.
if [[ ! -d "$JAR_DIR" ]]; then
    print -u2 "ERROR: $JAR_DIR not found. This does not look like an MH2p HMI layout. Aborting before any change."
    exit 4
fi
if [[ ! -d "$APPS_DIR" ]]; then
    print -u2 "ERROR: $APPS_DIR not found. This does not look like an MH2p HMI layout. Aborting before any change."
    exit 4
fi
if [[ ! -f "$APPS_DIR/gal" && ! -f "$APPS_DIR/gal.real" ]]; then
    print "WARN: $APPS_DIR/gal not found. Android Auto cluster video hook cannot be installed on this unit; BAP turn-by-turn (JAR) will still be installed."
fi

BACKUP_DIR="$MOD_PATH/Backup"
mkdir -p "$BACKUP_DIR" || { print -u2 "ERROR: cannot mkdir $BACKUP_DIR"; exit 2; }
mkdir -p "$CLUSTER_DIR"

ts() { date +"%Y%m%d_%H%M%S"; }

print "Slaying running processes before install..."
for proc in cluster gal gal.real dio_manager dio_manager.real; do
    slay -f "$proc" 2>/dev/null
    if [[ $? -eq 0 ]]; then
        print "slay:              $proc"
    fi
done
print ""

files_identical() {
    [[ -f "$1" && -f "$2" ]] || return 1
    sz1=$(wc -c < "$1" 2>/dev/null | awk '{print $1}')
    sz2=$(wc -c < "$2" 2>/dev/null | awk '{print $1}')
    [[ -n "$sz1" && "$sz1" = "$sz2" ]] || return 1
    s1=$(sum < "$1" 2>/dev/null)
    s2=$(sum < "$2" 2>/dev/null)
    if [[ -n "$s1" && -n "$s2" ]]; then
        [[ "$s1" = "$s2" ]]
    else
        return 0
    fi
}

# cmp first; if identical skip. Otherwise backup any existing DST and copy.
install_file() {
    src="$1"; dst="$2"
    if [[ ! -f "$src" ]]; then
        print "missing source:    $src"
        return 1
    fi
    if files_identical "$src" "$dst"; then
        print "skip identical:    $dst"
        return 0
    fi
    if [[ -f "$dst" ]]; then
        bk="$BACKUP_DIR/${dst##*/}.backup.$(ts)"
        cp -p "$dst" "$bk" || { print -u2 "ERROR: backup failed: $dst"; return 2; }
        print "backup target:     $dst -> $bk"
    fi
    cp -p "$src" "$dst" || { print -u2 "ERROR: copy failed: $src -> $dst"; return 3; }
    print "install:           $dst"
}

# Replace $APPS_DIR/NAME (binary) with the wrapper script from $MOD_PATH/NAME.
# Preserves the original binary at $APPS_DIR/NAME.real so uninstall can
# restore it. Idempotent — if NAME.real already exists, the original is
# already saved and we just refresh the wrapper.
swap_binary_for_wrapper() {
    name="$1"
    wrapper_src="$MOD_PATH/$name"
    dst_active="$APPS_DIR/$name"
    dst_real="$APPS_DIR/${name}.real"
    local_real_backup="$BACKUP_DIR/${name}.real"

    if [[ ! -f "$wrapper_src" ]]; then
        print "missing wrapper:   $wrapper_src"
        return 1
    fi

    if [[ -f "$dst_real" ]]; then
        # already swapped before; original safely at .real on device — never
        # overwrite. Always refresh BOTH local backups (stable-name + new
        # timestamped) from the device's .real so the modkit Backup folder
        # always has a current copy of the real binary.
        cp -p "$dst_real" "$local_real_backup" && \
            print "backup .real:      $dst_real -> $local_real_backup"
        bk="$BACKUP_DIR/${name}.original.$(ts)"
        cp -p "$dst_real" "$bk" && \
            print "backup .real (ts): $dst_real -> $bk"
        if files_identical "$wrapper_src" "$dst_active"; then
            print "skip identical:    $dst_active (wrapper already current)"
            return 0
        fi
        bk="$BACKUP_DIR/${name}.wrapper.backup.$(ts)"
        cp -p "$dst_active" "$bk" 2>/dev/null && print "backup wrapper:    $dst_active -> $bk"
        cp -p "$wrapper_src" "$dst_active" || { print -u2 "ERROR: wrapper copy failed"; return 2; }
        chmod 755 "$dst_active"
        print "refresh wrapper:   $dst_active"
        return 0
    fi

    # first install: original is still the real binary. Backup, then move.
    if [[ ! -f "$dst_active" ]]; then
        print -u2 "WARN: $dst_active not present — nothing to swap"
        return 0
    fi
    # Timestamped historical backup (each run gets its own).
    bk="$BACKUP_DIR/${name}.original.$(ts)"
    cp -p "$dst_active" "$bk" || { print -u2 "ERROR: backup of original $name failed"; return 2; }
    print "backup original:   $dst_active -> $bk"
    # Stable-name local backup (always reflects the original binary content).
    cp -p "$dst_active" "$local_real_backup" && \
        print "backup .real:      $dst_active -> $local_real_backup"
    mv "$dst_active" "$dst_real" || { print -u2 "ERROR: mv $name -> $name.real failed"; return 3; }
    print "preserve original: $dst_active -> $dst_real"
    cp -p "$wrapper_src" "$dst_active" || { print -u2 "ERROR: install wrapper failed"; return 4; }
    chmod 755 "$dst_active"
    print "install wrapper:   $dst_active"
}

NEW_JAR="$(ls -1t "$MOD_PATH"/ClusterIntegration_*.jar 2>/dev/null | head -1)"
[[ -z "$NEW_JAR" || ! -f "$NEW_JAR" ]] && {
    print -u2 "ERROR: no ClusterIntegration_*.jar in $MOD_PATH"
    exit 3
}

# Remove any older versions of our jar so only the new one remains.
typeset j
for j in "$JAR_DIR"/ClusterIntegration_* "$JAR_DIR"/AndroidAutoCluster_*; do
    if [[ -f "$j" && "${j##*/}" != "${NEW_JAR##*/}" ]]; then
        bk="$BACKUP_DIR/${j##*/}.backup.$(ts)"
        cp -p "$j" "$bk" && rm -f "$j" && print "remove old jar:    $j -> $bk"
    fi
done

install_file "$NEW_JAR"                  "$JAR_DIR/${NEW_JAR##*/}"
install_file "$MOD_PATH/cluster"         "$CLUSTER_DIR/cluster"
install_file "$MOD_PATH/gal_cluster.so"  "$CLUSTER_DIR/gal_cluster.so"
install_file "$MOD_PATH/dio_cluster.so"  "$CLUSTER_DIR/dio_cluster.so"

# cluster_config.json — read by Java (AA + CarPlay), cluster daemon, and
# gal_cluster.so at init time. Single source of truth for per-car settings.
# SD-card override path (/fs/sda0/cluster_config.json) takes precedence
# over this installed copy; readers fall through to defaults if neither
# exists. No longer embedded in the JAR.
install_file "$MOD_PATH/cluster_config.json" "$CLUSTER_DIR/cluster_config.json"

chmod 755 "$CLUSTER_DIR/cluster"        2>/dev/null
chmod 755 "$CLUSTER_DIR/gal_cluster.so" 2>/dev/null
chmod 755 "$CLUSTER_DIR/dio_cluster.so" 2>/dev/null
chmod 644 "$CLUSTER_DIR/cluster_config.json" 2>/dev/null

swap_binary_for_wrapper gal
swap_binary_for_wrapper dio_manager

sync
print ""
print "Done."

