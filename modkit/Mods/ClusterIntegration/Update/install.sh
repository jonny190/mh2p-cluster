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
# - Marker files next to the Update/ folder on the SD card:
#     force_install.txt   skip the OEM / firmware gate
#     diag.txt            hook logs only, no cluster service injected,
#                         installed config set to turn-by-turn only
#     enable_carplay.txt  install the CarPlay hook and full JAR on Audi
#     config_overrides.txt  key=value lines applied to the installed
#                         cluster_config.json "config" block (e.g. forceRHD=true)
#     uninstall.txt       ModKit convention: run uninstall.sh instead
# - Copies JAR, cluster, gal_cluster.so, dio_cluster.so to their target dirs.
# - Skips copies when the target file is byte-identical to the source.
# - Backs up any existing target before overwriting (timestamped, into
#   $MOD_PATH/Backup/).
# - Replaces /mnt/app/eso/bin/apps/gal with a wrapper script that sets
#   LD_PRELOAD=gal_cluster.so before execing the real gal binary.
#   The original binary is preserved at /mnt/app/eso/bin/apps/gal.real so
#   uninstall.sh can restore it.
# - Same for dio_manager.
# - Writes Backup/manifest.txt (add / replace / swap / remove lines with the
#   backup path for each) so every change can be rolled back; uninstall.sh
#   or rollback.sh performs the rollback.
#

set -u

export MOD_PATH="${modPath:-${MOD_PATH:-}}"
[[ -z "$MOD_PATH" ]] && { print -u2 "ERROR: MOD_PATH not set"; exit 1; }

[[ ! -e /mnt/app ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
mount -uw /mnt/app/

# ex: MH2p_US_PO416_P2870 (Porsche), MH2p_ER_AUG35_P2873 (Audi e-tron),
#     MH2p_ER_AU_P2873 (Audi units without a TYPE code, e.g. Q3/A1)
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
#   AU  Audi MH2p firmware 26xx / 27xx / 28xx        - EXPERIMENTAL
#       Firmware build numbers are one VAG-wide counter. Public e-tron GE
#       strings are AUG35 P2711 / P2718 / K2716_1 / P2873; P2873 sits in the
#       same build window as the Porsche P2870/P2874 the hooks were built
#       against and is the best-case target. MY2021+ e-tron is MIB3 (3xxx)
#       and out of scope.
# Anything else aborts unless force_install.txt is present.
case "$OEM" in
    PO|AU)
        if [[ "$OEM" == "PO" ]]; then
            brand="Porsche"; range="26xx / 28xx"
            in_range=0
            [[ "$SOFTWARE_VERSION" == 26?? || "$SOFTWARE_VERSION" == 28?? ]] && in_range=1
        else
            brand="Audi"; range="26xx / 27xx / 28xx"
            in_range=0
            [[ "$SOFTWARE_VERSION" == 26?? || "$SOFTWARE_VERSION" == 27?? || "$SOFTWARE_VERSION" == 28?? ]] && in_range=1
        fi
        if [[ $in_range -eq 0 ]]; then
            if [[ $FORCE_INSTALL -eq 1 ]]; then
                print "WARNING: $brand firmware $RELEASE_VERSION outside tested range ($range), continuing because force_install.txt is present."
            else
                print "Firmware $RELEASE_VERSION not in supported range ($range). Aborting."
                print "To override, create Mods/ClusterIntegration/force_install.txt on the SD card."
                exit 0
            fi
        else
            print "$brand firmware $RELEASE_VERSION OK, installing..."
        fi
        if [[ "$OEM" == "AU" ]]; then
            print "Audi support is EXPERIMENTAL: the JAR and native hooks were built against Porsche PCM5."
            print "If Android Auto or CarPlay stop working: put uninstall.txt in Mods/ClusterIntegration/ and re-run the ModKit."
        fi
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

# The mod's Java classes shadow the stock AndroidAuto2Subsystem and construct
# de.audi.app.car.adi.legacy.sportchrono.StorageMountHandler ("Sport Chrono"
# is a Porsche feature). If no HMI jar on this unit contains that class the
# shadowed subsystem dies with NoClassDefFoundError and Android Auto stops
# working. Jar directories are stored uncompressed, so grep -a finds the
# class path. Porsche: informational. Audi: abort unless forced.
check_hmi_class() {
    typeset cls hits
    cls="de/audi/app/car/adi/legacy/sportchrono/StorageMountHandler.class"
    hits="$(find "$JAR_DIR" -name '*.jar' 2>/dev/null | grep -v '/ClusterIntegration_' | while read j; do
        if grep -a -q -- "$cls" "$j" 2>/dev/null; then print "${j##*/}"; fi
    done | head -3 | tr '\n' ' ')"
    if [[ -n "$hits" ]]; then
        print "hmi class check:   StorageMountHandler found in: $hits"
        return 0
    fi
    print "hmi class check:   StorageMountHandler NOT found in any jar under $JAR_DIR"
    return 1
}
if ! check_hmi_class; then
    if [[ "$OEM" == "AU" && $FORCE_INSTALL -eq 0 ]]; then
        print "The mod's Java classes link against that Porsche-side class; installing would most likely break Android Auto on this unit."
        print "Aborting before any change. Create Mods/ClusterIntegration/force_install.txt to install anyway (rollback is available)."
        exit 0
    fi
    print "WARNING: continuing anyway (OEM=$OEM force=$FORCE_INSTALL)."
fi

BACKUP_DIR="$MOD_PATH/Backup"
mkdir -p "$BACKUP_DIR" || { print -u2 "ERROR: cannot mkdir $BACKUP_DIR"; exit 2; }
mkdir -p "$CLUSTER_DIR"

ts() { date +"%Y%m%d_%H%M%S"; }

# Change manifest: one line per file the installer adds, replaces, swaps or
# removes, with the backup copy it made. Appended on every run so the
# history of the head unit's modifications stays on the SD card.
# Rollback = uninstall.sh (via uninstall.txt + ModKit, or Update/rollback.sh
# over SSH): restores gal/dio_manager from .real (or from Backup/ if .real
# is gone) and removes everything listed as "add".
MANIFEST="$BACKUP_DIR/manifest.txt"
RUN_TS="$(ts)"
note() { print "$RUN_TS $*" >> "$MANIFEST" 2>/dev/null; }
note "run   install $RELEASE_VERSION"

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
        # Never overwrite a file whose backup is not a verified copy.
        files_identical "$dst" "$bk" || { print -u2 "ERROR: backup verify failed: $bk"; rm -f "$bk"; return 2; }
        print "backup target:     $dst -> $bk"
        note "replace $dst backup=$bk"
    else
        note "add $dst"
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
    files_identical "$dst_active" "$bk" || { print -u2 "ERROR: backup verify failed: $bk"; rm -f "$bk"; return 2; }
    print "backup original:   $dst_active -> $bk"
    # Stable-name local backup (always reflects the original binary content).
    cp -p "$dst_active" "$local_real_backup" && \
        print "backup .real:      $dst_active -> $local_real_backup"
    files_identical "$dst_active" "$local_real_backup" || { print -u2 "ERROR: backup verify failed: $local_real_backup"; return 2; }
    mv "$dst_active" "$dst_real" || { print -u2 "ERROR: mv $name -> $name.real failed"; return 3; }
    print "preserve original: $dst_active -> $dst_real"
    note "swap $dst_active original=$dst_real backup=$bk"
    cp -p "$wrapper_src" "$dst_active" || { print -u2 "ERROR: install wrapper failed"; return 4; }
    chmod 755 "$dst_active"
    print "install wrapper:   $dst_active"
}

NEW_JAR="$(ls -1t "$MOD_PATH"/ClusterIntegration_*.jar 2>/dev/null | head -1)"
[[ -z "$NEW_JAR" || ! -f "$NEW_JAR" ]] && {
    print -u2 "ERROR: no ClusterIntegration_*.jar in $MOD_PATH"
    exit 3
}

# Java classes: on Audi without enable_carplay.txt install the Android-Auto-
# only JAR (Update/aa_only/*_aa.jar, built by modkit/build_sd_package.sh). It
# lacks the CarPlay classes, so the stock CarPlayDSIManager stays in charge
# of CarPlay and only AndroidAuto2Subsystem is shadowed. The full JAR is used
# on Porsche and on Audi with enable_carplay.txt.
CARPLAY_HOOK=1
[[ "$OEM" == "AU" && ! -e "$MOD_ROOT/enable_carplay.txt" ]] && CARPLAY_HOOK=0
if [[ $CARPLAY_HOOK -eq 0 ]]; then
    AA_JAR="$(ls -1t "$MOD_PATH"/aa_only/ClusterIntegration_*_aa.jar 2>/dev/null | head -1)"
    if [[ -n "$AA_JAR" && -f "$AA_JAR" ]]; then
        NEW_JAR="$AA_JAR"
        print "java classes:      Android Auto only (${NEW_JAR##*/}; CarPlay classes not installed)"
    else
        print "WARN: aa_only/ClusterIntegration_*_aa.jar not found, installing the full JAR (CarPlay classes included)"
    fi
else
    print "java classes:      full JAR (${NEW_JAR##*/})"
fi

# Remove any older versions of our jar so only the new one remains.
typeset j
for j in "$JAR_DIR"/ClusterIntegration_* "$JAR_DIR"/AndroidAutoCluster_*; do
    if [[ -f "$j" && "${j##*/}" != "${NEW_JAR##*/}" ]]; then
        bk="$BACKUP_DIR/${j##*/}.backup.$(ts)"
        cp -p "$j" "$bk" && rm -f "$j" && print "remove old jar:    $j -> $bk" && note "remove $j backup=$bk"
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

# Per-car config overrides. Mods/ClusterIntegration/config_overrides.txt on
# the SD card holds "key=value" lines (value written as JSON: true, false,
# a number, or "a string"; '#' starts a comment). Each key is replaced in
# the top-level "config" block of the installed cluster_config.json only
# (scalar keys such as forceRHD, forceImperial, imperialSmallUnit,
# bargraphMode, enableMapRender, aaClusterMode, heartbeatInterval). Nested
# objects (mirror, gal_h264) and carConfig entries are not touched; use the
# SD-card override file /fs/sda0/cluster_config.json for those. Re-running
# without the file reinstalls the shipped config.
OVERRIDES="$MOD_ROOT/config_overrides.txt"
if [[ -f "$OVERRIDES" ]]; then
    cfg="$CLUSTER_DIR/cluster_config.json"
    grep -v '^[ 	]*#' "$OVERRIDES" | grep '=' | while IFS='=' read -r k v; do
        k="$(print -r -- "$k" | sed -e 's/^[ 	]*//' -e 's/[ 	]*$//')"
        v="$(print -r -- "$v" | sed -e 's/^[ 	]*//' -e 's/[ 	]*$//')"
        [[ -z "$k" || -z "$v" ]] && continue
        if awk -v key="$k" -v val="$v" '
            BEGIN { inblk = 0; done = 0 }
            /"config"[ \t]*:[ \t]*\{/ { inblk = 1 }
            inblk && !done {
                pat = "\"" key "\"[ \t]*:[ \t]*[^,}]*"
                if (match($0, pat)) {
                    old = substr($0, RSTART, RLENGTH)
                    sub(/^"[^"]*"[ \t]*:[ \t]*/, "", old)
                    if (old !~ /^[\[{]/) {
                        $0 = substr($0, 1, RSTART - 1) "\"" key "\": " val substr($0, RSTART + RLENGTH)
                        done = 1
                    }
                }
            }
            inblk && /^[ \t]*\},?[ \t]*$/ { inblk = 0 }
            { print }
            END { if (!done) exit 3 }
        ' "$cfg" > "$cfg.ovr"; then
            mv -f "$cfg.ovr" "$cfg" && chmod 644 "$cfg" 2>/dev/null
            print "config override:   $k = $v"
            note "override $cfg $k=$v"
        else
            rm -f "$cfg.ovr"
            print "config override:   WARN '$k' is not a scalar key of the config block, ignored"
        fi
    done
fi

swap_binary_for_wrapper gal

# CarPlay hook (dio_manager wrapper + dio_cluster.so). On Porsche it is always
# installed. On Audi it is opt-in via Mods/ClusterIntegration/enable_carplay.txt:
# the hook patches every iAP2 Identify inside the wireless-CarPlay process with
# Porsche-derived object layouts, so on an untested unit it is risk without
# benefit for an Android Auto user. Without the marker any earlier wrapper is
# restored so the unit runs the stock dio_manager.
if [[ $CARPLAY_HOOK -eq 1 ]]; then
    swap_binary_for_wrapper dio_manager
    print "carplay hook:      ON"
else
    if [[ -f "$APPS_DIR/dio_manager.real" ]]; then
        bk="$BACKUP_DIR/dio_manager.wrapper.removed.$(ts)"
        cp -p "$APPS_DIR/dio_manager" "$bk" 2>/dev/null
        mv "$APPS_DIR/dio_manager.real" "$APPS_DIR/dio_manager" && \
            print "restore original:  $APPS_DIR/dio_manager.real -> $APPS_DIR/dio_manager (carplay hook disabled)" && \
            note "restore $APPS_DIR/dio_manager from=$APPS_DIR/dio_manager.real"
    fi
    print "carplay hook:      OFF (Audi default; create Mods/ClusterIntegration/enable_carplay.txt to install it)"
fi

# Diagnostic mode. If Mods/ClusterIntegration/diag.txt exists on the SD card,
# leave a marker that makes the gal wrapper start the hook with
# GAL_CLUSTER_MERGE=0 (no cluster service injected into the phone's service
# discovery, no endpoint table write) plus full logging, and the dio_manager
# wrapper with DIO_CLUSTER_LOG=1. Android Auto then behaves as stock while
# /tmp/gal_cluster.log shows whether the hook loaded, which symbols resolved
# and what the stock service discovery looks like. Re-run without diag.txt
# (or delete the marker over SSH) to go live.
DIAG_MARKER="$CLUSTER_DIR/diag_mode"
if [[ -e "$MOD_ROOT/diag.txt" ]]; then
    if [[ ! -f "$DIAG_MARKER" ]]; then
        : > "$DIAG_MARKER" && chmod 644 "$DIAG_MARKER" && note "add $DIAG_MARKER"
    fi
    # Also switch the installed config to BAP-only ("enableMapRender": false):
    # with the cluster service not injected there is no video to show, and
    # an empty cluster window must not be posted over the native map. The
    # next non-diag run reinstalls the shipped config (install_file sees the
    # difference, backs this copy up and overwrites it).
    if sed -e 's/"enableMapRender"[ ]*:[ ]*true/"enableMapRender": false/' "$CLUSTER_DIR/cluster_config.json" > "$CLUSTER_DIR/cluster_config.json.diag" 2>/dev/null \
       && grep -q '"enableMapRender": false' "$CLUSTER_DIR/cluster_config.json.diag"; then
        mv -f "$CLUSTER_DIR/cluster_config.json.diag" "$CLUSTER_DIR/cluster_config.json"
        chmod 644 "$CLUSTER_DIR/cluster_config.json" 2>/dev/null
        note "replace $CLUSTER_DIR/cluster_config.json diag=enableMapRender:false"
        print "diag mode:         installed cluster_config.json has enableMapRender=false (turn-by-turn only)"
    else
        rm -f "$CLUSTER_DIR/cluster_config.json.diag"
        print "diag mode:         WARN could not rewrite cluster_config.json; video path left enabled"
    fi
    print "diag mode:         ON  ($DIAG_MARKER) - hook logs only, cluster service NOT injected"
    # Stage the stock HMI jars that contain the classes this mod shadows, so
    # constructor signatures can be compared on a PC with javap
    # (modkit/check_hmi_signatures.sh) before going live.
    REF_DIR="$BACKUP_DIR/hmi_reference"
    mkdir -p "$REF_DIR" 2>/dev/null
    typeset refcls refjar refn
    refn=0
    for refcls in \
        de/audi/app/terminalmode/smartphone/androidauto2/AndroidAuto2Subsystem.class \
        de/audi/app/terminalmode/smartphone/carplay/CarPlayDSIManager.class \
    ; do
        find "$JAR_DIR" -name '*.jar' 2>/dev/null | grep -v '/ClusterIntegration_' | while read refjar; do
            if grep -a -q -- "$refcls" "$refjar" 2>/dev/null; then
                if [[ ! -f "$REF_DIR/${refjar##*/}" ]]; then
                    cp -p "$refjar" "$REF_DIR/${refjar##*/}" && print "hmi reference:     ${refjar##*/} -> Backup/hmi_reference/ (contains ${refcls##*/})"
                fi
            fi
        done
    done
    refn="$(ls -1 "$REF_DIR" 2>/dev/null | wc -l | awk '{print $1}')"
    print "hmi reference:     $refn stock jar(s) staged for modkit/check_hmi_signatures.sh"
else
    if [[ -f "$DIAG_MARKER" ]]; then
        rm -f "$DIAG_MARKER" && note "remove $DIAG_MARKER"
        print "diag mode:         OFF (marker removed)"
    else
        print "diag mode:         OFF"
    fi
fi

# Informational: does this unit's Android Auto receiver export the symbols
# gal_cluster.so interposes? A missing symbol means that hook is a silent
# pass-through (cluster stays blank), never a crash. Object layouts cannot
# be checked here. Output goes to the ModKit log for the first-boot review.
check_hook_symbols() {
    typeset lib sym c present missing
    lib="$(find /mnt/app -name 'libautoreceiver*' 2>/dev/null | head -1)"
    if [[ -z "$lib" ]]; then
        print "hook symbols:      libautoreceiver not found under /mnt/app (check skipped)"
        return 0
    fi
    present=0; missing=""
    for sym in \
        _ZN13MessageRouter32populateServiceDiscoveryResponseEP24ServiceDiscoveryResponse \
        _ZN13MessageRouter13queueOutgoingEhPvj \
        _ZN13MessageRouter19sendChannelOpenRespEhi \
        _ZN13MessageRouter20handleChannelOpenReqEhRK18ChannelOpenRequest \
        _ZN13MessageRouter12routeMessageEhRK10shared_ptrI8IoBufferE \
        _ZN10Controller12routeMessageEhtRK10shared_ptrI8IoBufferE \
        _ZN13MediaSinkBase12routeMessageEhtRK10shared_ptrI8IoBufferE \
        _ZN24NavigationStatusEndpoint16addDiscoveryInfoEP24ServiceDiscoveryResponse \
        _ZN24NavigationStatusEndpoint22handleNavigationStatusERK16NavigationStatus \
        _ZN24NavigationStatusEndpoint29handleNavigationDistanceEventERK31NavigationNextTurnDistanceEvent \
        _ZN24NavigationStatusEndpoint29handleNavigationNextTurnEventERK23NavigationNextTurnEvent \
        _ZN24NavigationStatusEndpoint4stopEv \
        _ZN24NavigationStatusEndpoint5startEv \
        _ZN27NavFocusRequestNotification27MergePartialFromCodedStreamEPN6google8protobuf2io16CodedInputStreamE \
    ; do
        c="$(grep -a -c -- "$sym" "$lib" 2>/dev/null)"
        if [[ $? -gt 1 ]]; then
            print "hook symbols:      grep -a unavailable on this unit (check skipped)"
            return 0
        fi
        if [[ "$c" != "0" && -n "$c" ]]; then
            present=$((present + 1))
        else
            missing="$missing $sym"
        fi
    done
    print "hook symbols:      $present/14 present in $lib"
    [[ -n "$missing" ]] && print "hook symbols:      missing:$missing"
    return 0
}
check_hook_symbols

sync
print ""
print "Backups:           $BACKUP_DIR (manifest.txt lists every change)"
print "Diagnostics:       put diag.txt in Mods/ClusterIntegration/ and re-run to log without injecting (see README)"
print "Rollback:          put uninstall.txt in Mods/ClusterIntegration/ and re-run the ModKit,"
print "                   or over SSH: ksh $MOD_PATH/rollback.sh"
print "Done."

