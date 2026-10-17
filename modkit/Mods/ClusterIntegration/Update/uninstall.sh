#!/bin/ksh
# Copyright (c) 2026 fifthBro. Local recovery changes: CC BY-NC-SA 4.0.
# https://fifthbro.github.io ; see LICENSE.md. NOT FOR COMMERCIAL USE.
# --check is read-only except SD logs; --self-test uses isolated SD files.
set -u
export MOD_PATH="${modPath:-${MOD_PATH:-}}"
[[ -n "$MOD_PATH" && -f "$MOD_PATH/logging.sh" ]] || { print -u2 "ERROR: MOD_PATH/logging.sh missing"; exit 1; }
MOD_PATH="$(cd "$MOD_PATH" && pwd)" || exit 1
export PATH="${PATH:-/bin:/usr/bin}:/mnt/app/armle/usr/bin:/mnt/app/armle/bin:/usr/bin:/bin"
. "$MOD_PATH/logging.sh"
ci_log_init rollback || exit 1
CI_CHECK=0
CI_SELFTEST=0
case "${1:-}" in
    '') [[ $# -eq 0 ]] || exit 2 ;;
    --check) [[ $# -eq 1 ]] || exit 2; CI_CHECK=1 ;;
    --self-test) [[ $# -eq 1 ]] || exit 2; CI_SELFTEST=1 ;;
    *) ci_log "ERROR unknown argument"; exit 2 ;;
esac
APPS_DIR=/mnt/app/eso/bin/apps
JAR_DIR=/mnt/app/eso/hmi/lsd/jars
CLUSTER_DIR="$APPS_DIR/cluster"
BACKUP_DIR="$MOD_PATH/Backup"
STOCK_DIR="$MOD_PATH/Stock"
ci_run mkdir -p "$BACKUP_DIR" || exit 2
MANIFEST="$BACKUP_DIR/manifest.txt"
note() { print -r -- "$CI_RUN_ID $*" >> "$MANIFEST"; }
fail() { ci_log "ERROR $*"; CI_RESULT=FAILED; }

# Do not use size/wc counts to verify restored executables.
ci_run mkdir "$CI_RUN_DIR/compare-probe" || exit 2
print -r -- 'probe-A' > "$CI_RUN_DIR/compare-probe/a"
print -r -- 'probe-B' > "$CI_RUN_DIR/compare-probe/b"
COMPARE=""
same_with() {
    typeset tool=$1 a=$2 b=$3 ha hb
    case "$tool" in
        cmp) cmp -s "$a" "$b" ;;
        diff) diff "$a" "$b" >/dev/null 2>&1 ;;
        sha256sum|md5sum)
            ha=$("$tool" < "$a") || return 2
            hb=$("$tool" < "$b") || return 2
            [[ -n "$ha" && "$ha" == "$hb" ]] ;;
        *) return 2 ;;
    esac
}
for tool in cmp sha256sum md5sum diff; do
    ci_have "$tool" || continue
    if same_with "$tool" "$CI_RUN_DIR/compare-probe/a" "$CI_RUN_DIR/compare-probe/a"; then
        same_with "$tool" "$CI_RUN_DIR/compare-probe/a" "$CI_RUN_DIR/compare-probe/b"
        [[ $? -eq 1 ]] && { COMPARE=$tool; break; }
    fi
done
[[ -n "$COMPARE" ]] || { fail "No working byte/digest comparison; live files untouched"; exit 3; }
ci_log "COMPARISON_TOOL=$COMPARE"
same() {
    ci_log "VERIFY_BYTES source=$1 destination=$2 tool=$COMPARE"
    same_with "$COMPARE" "$1" "$2"
    typeset rc=$?
    ci_log "VERIFY_RESULT rc=$rc source=$1 destination=$2"
    return "$rc"
}
is_wrapper() {
    [[ -f "$1" ]] && [[ "$(dd if="$1" bs=1 count=10 2>/dev/null)" == '#!/bin/ksh' ]]
}
validate_stock_gal() {
    [[ -f "$STOCK_DIR/gal.real" && -f "$STOCK_DIR/gal.reference" ]] || { fail "Saved SD GAL/reference missing"; return 1; }
    [[ "$(dd if="$STOCK_DIR/gal.real" bs=1 count=4 2>/dev/null)" == $'\177ELF' ]] || { fail "Saved GAL is not ELF"; return 1; }
    same "$STOCK_DIR/gal.real" "$STOCK_DIR/gal.reference" || return 1
    if ci_have sha256sum; then
        typeset digest
        digest=$(sha256sum < "$STOCK_DIR/gal.real") || return 1
        case "$digest" in
            9d455b764854f7977971ae977b21fe7560d8a40cca86d31cb9c9a7f5821c0ecf*) ci_log "STOCK_GAL_SHA256 verified" ;;
            *) fail "Saved GAL SHA256 differs from P2873 evidence"; return 1 ;;
        esac
    else
        ci_log "STOCK_GAL_REFERENCE verified; runtime SHA256 unavailable; check package SHA256SUMS on PC"
    fi
}
if [[ $CI_SELFTEST -eq 1 ]]; then
    validate_stock_gal || exit 3
    BACKUP_DIR="$CI_RUN_DIR/sandbox-backups"
    MANIFEST="$BACKUP_DIR/manifest.txt"
    ci_run mkdir "$BACKUP_DIR" || exit 2
    APPS_DIR="$CI_RUN_DIR/sandbox-root/apps"
    JAR_DIR="$CI_RUN_DIR/sandbox-root/jars"
    CLUSTER_DIR="$APPS_DIR/cluster"
    ci_run mkdir -p "$CLUSTER_DIR" "$JAR_DIR" || exit 2
    ci_run cp -p "$STOCK_DIR/gal.real" "$APPS_DIR/gal.real" || exit 2
    print -r -- '#!/bin/ksh' > "$APPS_DIR/gal"
    for name in cluster gal_cluster.so dio_cluster.so cluster_config.json diag_mode gal_env; do
        print -r -- 'sandbox helper' > "$CLUSTER_DIR/$name"
    done
    print -r -- 'sandbox jar' > "$JAR_DIR/ClusterIntegration_selftest.jar"
    print -r -- 'sandbox stub' > "$JAR_DIR/ClusterIntegration_StorageMountHandler_stub.jar"
    print -r -- 'preserve NavCompass' > "$JAR_DIR/NaviCompass.jar"
    print -r -- 'preserve stock jar' > "$JAR_DIR/AppDevelopment.jar"
    ci_log "SELFTEST_SCOPE=$CI_RUN_DIR/sandbox-root"
fi
[[ ! -f "$STOCK_DIR/gal.real" ]] || validate_stock_gal || exit 3
GAL_SOURCE=""
DIO_SOURCE=""
choose_original() {
    typeset name=$1
    ORIGINAL=""
    if [[ -f "$APPS_DIR/$name.real" ]] || is_wrapper "$APPS_DIR/$name"; then
        if [[ "$name" == gal && -f "$STOCK_DIR/gal.real" ]]; then
            validate_stock_gal || return 1
            ORIGINAL="$STOCK_DIR/gal.real"
        elif [[ -f "$APPS_DIR/$name.real" ]]; then
            ORIGINAL="$APPS_DIR/$name.real"
        elif [[ -f "$BACKUP_DIR/$name.real" ]]; then
            ORIGINAL="$BACKUP_DIR/$name.real"
        else
            fail "Cannot restore $name: originals missing"; return 1
        fi
        [[ "$(dd if="$ORIGINAL" bs=1 count=4 2>/dev/null)" == $'\177ELF' ]] || { fail "Original $name is not ELF"; return 1; }
        ci_log "RESTORE_PLAN name=$name source=$ORIGINAL destination=$APPS_DIR/$name"
    else
        ci_log "RESTORE_SKIP $name has no wrapper/.real"
    fi
}
choose_original gal || exit 3
GAL_SOURCE=$ORIGINAL
choose_original dio_manager || exit 3
DIO_SOURCE=$ORIGINAL
ci_log "PREFLIGHT_OK check=$CI_CHECK selftest=$CI_SELFTEST"
if [[ $CI_CHECK -eq 1 ]]; then CI_RESULT=CHECK_OK; exit 0; fi
if [[ $CI_SELFTEST -eq 0 ]]; then
    # Leave the card in uninstall mode to avoid reinstall on the next boot.
    print -r -- 'stock rollback requested' > "$MOD_PATH/../uninstall.txt" || exit 2
    # Confirmed path/marker contract in the packaged ModKit persist script.
    # This disables this mod's installed Persist step without deleting it.
    if [[ -d /mnt/ota/modkit/Mods/ClusterIntegration ]]; then
        print -r -- 'stock rollback requested' > /mnt/ota/modkit/Mods/ClusterIntegration/uninstall.txt || { fail "Cannot disable installed Persist step"; exit 3; }
        ci_log "PERSIST_DISABLED /mnt/ota/modkit/Mods/ClusterIntegration/uninstall.txt"
    fi
    [[ -d /mnt/app/eso/hmi/lsd ]] || { fail "Expected application filesystem missing; no guessed block-device mount"; exit 3; }
    ci_run mount -uw /mnt/app/ || { fail "Application remount failed"; exit 3; }
fi
backup_file() {
    typeset src=$1 dst="$BACKUP_DIR/${1##*/}.removed.$(ci_stamp)"
    ci_run cp -p "$src" "$dst" || return 1
    same "$src" "$dst" || return 1
    note "backup $src copy=$dst" || return 1
}
stage_original() {
    typeset name=$1 original=$2 stage="$APPS_DIR/$1.mib2p-restore-stage"
    [[ -n "$original" ]] || return 0
    [[ ! -e "$stage" ]] || { fail "Existing recovery stage retained: $stage"; return 1; }
    [[ ! -f "$APPS_DIR/$name" ]] || backup_file "$APPS_DIR/$name" || return 1
    ci_run cp -p "$original" "$stage" || return 1
    same "$original" "$stage" || return 1
    ci_run chmod 755 "$stage" || return 1
    ci_log "STAGE_READY $stage"
}
stage_original gal "$GAL_SOURCE" || { fail "GAL staging failed; active GAL untouched"; exit 4; }
stage_original dio_manager "$DIO_SOURCE" || { fail "DIO staging failed; active binaries untouched"; exit 4; }
if [[ $CI_SELFTEST -eq 0 ]]; then
    ci_run slay -f cluster || ci_log "PROCESS_ABSENT_OR_STOP_FAILED cluster"
    if [[ -n "$GAL_SOURCE" ]]; then
        ci_run slay -f gal || ci_log "PROCESS_ABSENT_OR_STOP_FAILED gal"
        ci_run slay -f gal.real || ci_log "PROCESS_ABSENT_OR_STOP_FAILED gal.real"
    fi
    if [[ -n "$DIO_SOURCE" ]]; then
        ci_run slay -f dio_manager || ci_log "PROCESS_ABSENT_OR_STOP_FAILED dio_manager"
        ci_run slay -f dio_manager.real || ci_log "PROCESS_ABSENT_OR_STOP_FAILED dio_manager.real"
    fi
else
    ci_log "SELFTEST_SKIP mount and process signals"
fi
restore_binary() {
    typeset name=$1 original=$2
    [[ -n "$original" ]] || return 0
    ci_run mv -f "$APPS_DIR/$name.mib2p-restore-stage" "$APPS_DIR/$name" || return 1
    same "$original" "$APPS_DIR/$name" || return 1
    # Retain .real as an additional original; never move the last good copy.
    # If that unit copy differs from our verified SD original, preserve the
    # differing bytes in Backup and refresh it. A future installer must not
    # wrap a good active binary around a corrupt .real left from a failure.
    if [[ -f "$APPS_DIR/$name.real" ]] && ! same "$original" "$APPS_DIR/$name.real"; then
        backup_file "$APPS_DIR/$name.real" || return 1
        ci_run cp -p "$original" "$APPS_DIR/$name.mib2p-restore-stage" || return 1
        same "$original" "$APPS_DIR/$name.mib2p-restore-stage" || return 1
        ci_run chmod 755 "$APPS_DIR/$name.mib2p-restore-stage" || return 1
        ci_run mv -f "$APPS_DIR/$name.mib2p-restore-stage" "$APPS_DIR/$name.real" || return 1
        same "$original" "$APPS_DIR/$name.real" || return 1
    fi
    note "restore $APPS_DIR/$name from=$original" || return 1
    ci_log "RESTORED_AND_VERIFIED $name"
}
restore_binary gal "$GAL_SOURCE" || { fail "GAL restore failed; mod helpers retained"; exit 5; }
restore_binary dio_manager "$DIO_SOURCE" || { fail "DIO restore failed; mod helpers retained"; exit 5; }
ERRORS=0
remove_added() {
    typeset f=$1
    [[ -f "$f" ]] || { ci_log "REMOVE_SKIP absent=$f"; return 0; }
    backup_file "$f" || { fail "Backup failed; retaining $f"; return 1; }
    ci_run rm -f "$f" || return 1
    [[ ! -e "$f" ]] || return 1
    note "remove $f" || return 1
    ci_log "REMOVED $f"
}
for name in cluster gal_cluster.so dio_cluster.so cluster_config.json diag_mode gal_env; do
    remove_added "$CLUSTER_DIR/$name" || ERRORS=$((ERRORS + 1))
done
for jar in "$JAR_DIR"/ClusterIntegration_* "$JAR_DIR"/AndroidAutoCluster_*; do
    [[ -f "$jar" ]] || continue
    remove_added "$jar" || ERRORS=$((ERRORS + 1))
done
# Fixed allowlist covers this candidate. Never execute mutable manifest
# paths, and never remove NavCompass or stock JARs.
ci_run sync || ERRORS=$((ERRORS + 1))
if [[ $ERRORS -ne 0 ]]; then fail "ROLLBACK_ERRORS=$ERRORS"; exit 6; fi
if [[ $CI_SELFTEST -eq 1 ]]; then
    same "$STOCK_DIR/gal.real" "$APPS_DIR/gal" || exit 6
    [[ ! -e "$CLUSTER_DIR/cluster" && ! -e "$JAR_DIR/ClusterIntegration_selftest.jar" ]] || exit 6
    [[ -f "$JAR_DIR/NaviCompass.jar" && -f "$JAR_DIR/AppDevelopment.jar" ]] || exit 6
    CI_RESULT=SELFTEST_OK
else
    CI_RESULT=ROLLBACK_OK
fi
ci_log "STOCK_ROLLBACK_COMPLETE no automatic reboot; restart MMI normally to reload stock Java"
exit 0
