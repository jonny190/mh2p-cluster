#!/bin/ksh
# Copyright (c) 2026 fifthBro. Recovery improvements: CC BY-NC-SA 4.0.
# See LICENSE.md. NOT FOR COMMERCIAL USE.
# --check writes SD logs/probes only; --self-test uses an isolated SD sandbox.
set -u
export MOD_PATH="${modPath:-${MOD_PATH:-}}"
[[ -n "$MOD_PATH" && -f "$MOD_PATH/logging.sh" && -f "$MOD_PATH/comparison.sh" ]] || { print -u2 'ERROR: MOD_PATH/logging.sh/comparison.sh missing'; exit 1; }
MOD_PATH="$(cd "$MOD_PATH" && pwd)" || exit 1
export PATH="${PATH:-/bin:/usr/bin}:/mnt/app/armle/usr/bin:/mnt/app/armle/bin:/usr/bin:/bin"
. "$MOD_PATH/logging.sh"
ci_log_init rollback || exit 1
CI_CHECK=0; CI_SELFTEST=0
case "${1:-}" in
    '') [[ $# -eq 0 ]] || exit 2 ;;
    --check) [[ $# -eq 1 ]] || exit 2; CI_CHECK=1 ;;
    --self-test) [[ $# -eq 1 ]] || exit 2; CI_SELFTEST=1 ;;
    *) ci_log 'ERROR unknown argument'; exit 2 ;;
esac
APPS_DIR=/mnt/app/eso/bin/apps
JAR_DIR=/mnt/app/eso/hmi/lsd/jars
CLUSTER_DIR="$APPS_DIR/cluster"
BACKUP_DIR="$MOD_PATH/Backup"
ci_run mkdir -p "$BACKUP_DIR" || exit 2
MANIFEST="$BACKUP_DIR/manifest.txt"
note() { print -r -- "$CI_RUN_ID $*" >> "$MANIFEST"; }
fail() { ci_log "ERROR $*"; CI_RESULT=FAILED; }
. "$MOD_PATH/comparison.sh"
ci_compare_init || { fail 'No working strong comparison; live files untouched'; exit 3; }
[[ -f "$MOD_PATH/elf-magic.bin" ]] || { fail 'ELF header fixture missing'; exit 3; }
same() {
    ci_log "VERIFY_BYTES source=$1 destination=$2 tool=$CI_COMPARE_TOOL"
    ci_compare "$1" "$2"
    typeset rc=$?
    ci_log "VERIFY_RESULT rc=$rc source=$1 destination=$2"
    return "$rc"
}
is_wrapper() { [[ -f "$1" && "$(dd if="$1" bs=1 count=10 2>/dev/null)" == '#!/bin/ksh' ]]; }
is_elf() {
    typeset stamp header
    stamp=$(ci_stamp) || return 1
    header="$CI_RUN_DIR/elf-header-$stamp.bin"
    [[ -f "$MOD_PATH/elf-magic.bin" ]] || return 1
    ci_run dd if="$1" of="$header" bs=1 count=4 || return 1
    same "$header" "$MOD_PATH/elf-magic.bin"
}
if [[ $CI_SELFTEST -eq 1 ]]; then
    # These are non-executable ELF-magic fixtures, never run as programs.
    BACKUP_DIR="$CI_RUN_DIR/sandbox-backups"; MANIFEST="$BACKUP_DIR/manifest.txt"
    APPS_DIR="$CI_RUN_DIR/sandbox-root/apps"; JAR_DIR="$CI_RUN_DIR/sandbox-root/jars"
    CLUSTER_DIR="$APPS_DIR/cluster"
    ci_run mkdir -p "$BACKUP_DIR" "$CLUSTER_DIR" "$JAR_DIR" || exit 2
    for name in gal dio_manager; do
        ci_run cp "$MOD_PATH/selftest-original.bin" "$APPS_DIR/$name.real" || exit 2
        print -r -- '#!/bin/ksh' > "$APPS_DIR/$name" || exit 2
    done
    for name in cluster gal_cluster.so dio_cluster.so cluster_config.json diag_mode daemon_verbose gal_env; do
        print -r -- 'sandbox mod file' > "$CLUSTER_DIR/$name" || exit 2
    done
    for name in ClusterIntegration_selftest.jar ClusterIntegration_StorageMountHandler_stub.jar AndroidAutoCluster_selftest.jar NaviCompass.jar AppDevelopment.jar; do
        print -r -- "$name" > "$JAR_DIR/$name" || exit 2
    done
    ci_log "SELFTEST_SCOPE=$CI_RUN_DIR/sandbox-root synthetic_data=true"
fi
ORIGINAL=""
choose_original() {
    typeset name=$1
    ORIGINAL=""
    if [[ -f "$APPS_DIR/$name.real" ]]; then
        ORIGINAL="$APPS_DIR/$name.real"
    elif is_wrapper "$APPS_DIR/$name"; then
        [[ -f "$BACKUP_DIR/$name.real" ]] || { fail "Cannot restore $name: .real and SD backup missing"; return 1; }
        ORIGINAL="$BACKUP_DIR/$name.real"
    else
        ci_log "RESTORE_SKIP name=$name no_wrapper_or_saved_original"
        return 0
    fi
    is_elf "$ORIGINAL" || { fail "Original $name ELF header check failed"; return 1; }
    ci_log "RESTORE_PLAN name=$name source=$ORIGINAL destination=$APPS_DIR/$name"
}
choose_original gal || exit 3; GAL_SOURCE=$ORIGINAL
choose_original dio_manager || exit 3; DIO_SOURCE=$ORIGINAL
ci_log "PREFLIGHT_OK check=$CI_CHECK selftest=$CI_SELFTEST"
if [[ $CI_CHECK -eq 1 ]]; then CI_RESULT=CHECK_OK; exit 0; fi
if [[ $CI_SELFTEST -eq 0 ]]; then
    [[ -d "$APPS_DIR" && -d "$JAR_DIR" ]] || { fail 'Application filesystem missing; no guessed device mount'; exit 3; }
    ci_run mount -uw /mnt/app/ || { fail 'Application remount failed'; exit 3; }
fi
backup_file() {
    typeset src=$1 stamp dst
    stamp=$(ci_stamp) || return 1
    dst="$BACKUP_DIR/${src##*/}.removed.$stamp"
    ci_run cp -p "$src" "$dst" || return 1
    same "$src" "$dst" || return 1
    note "backup $src copy=$dst" || return 1
}
stage_original() {
    typeset name=$1 original=$2 stage="$APPS_DIR/$1.mib2p-restore-stage"
    [[ -n "$original" ]] || return 0
    [[ ! -e "$stage" ]] || { fail "Existing stage retained: $stage"; return 1; }
    [[ ! -f "$APPS_DIR/$name" ]] || backup_file "$APPS_DIR/$name" || return 1
    ci_run cp -p "$original" "$stage" || return 1
    same "$original" "$stage" || return 1
    ci_run chmod 755 "$stage" || return 1
    ci_log "STAGE_READY $stage"
}
# Both executables are staged and verified before any process is stopped.
stage_original gal "$GAL_SOURCE" || { fail 'GAL staging failed; live files untouched'; exit 4; }
stage_original dio_manager "$DIO_SOURCE" || { fail 'DIO staging failed; live files untouched'; exit 4; }
if [[ $CI_SELFTEST -eq 0 ]]; then
    ci_run slay -f cluster || ci_log 'PROCESS_ABSENT_OR_STOP_FAILED cluster'
    if [[ -n "$GAL_SOURCE" ]]; then
        ci_run slay -f gal || ci_log 'PROCESS_ABSENT_OR_STOP_FAILED gal'
        ci_run slay -f gal.real || ci_log 'PROCESS_ABSENT_OR_STOP_FAILED gal.real'
    fi
    if [[ -n "$DIO_SOURCE" ]]; then
        ci_run slay -f dio_manager || ci_log 'PROCESS_ABSENT_OR_STOP_FAILED dio_manager'
        ci_run slay -f dio_manager.real || ci_log 'PROCESS_ABSENT_OR_STOP_FAILED dio_manager.real'
    fi
else
    ci_log 'SELFTEST_SKIP mount and process signals'
fi
restore_binary() {
    typeset name=$1 original=$2
    [[ -n "$original" ]] || return 0
    ci_run mv -f "$APPS_DIR/$name.mib2p-restore-stage" "$APPS_DIR/$name" || return 1
    same "$original" "$APPS_DIR/$name" || return 1
    # Keep the last good .real / SD backup, instead of consuming it with mv.
    note "restore $APPS_DIR/$name from=$original" || return 1
    ci_log "RESTORED_AND_VERIFIED $name"
}
restore_binary gal "$GAL_SOURCE" || { fail 'GAL restore failed; helpers retained'; exit 5; }
restore_binary dio_manager "$DIO_SOURCE" || { fail 'DIO restore failed; helpers retained'; exit 5; }
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
# Remove known mod files, then constrained future additions recorded below.
for name in cluster gal_cluster.so dio_cluster.so cluster_config.json diag_mode daemon_verbose gal_env; do
    remove_added "$CLUSTER_DIR/$name" || ERRORS=$((ERRORS + 1))
done
for jar in "$JAR_DIR"/ClusterIntegration_* "$JAR_DIR"/AndroidAutoCluster_*; do
    [[ -f "$jar" ]] || continue
    remove_added "$jar" || ERRORS=$((ERRORS + 1))
done
# Retain upstream's future-file cleanup, restricted to immediate children of
# the mod directory. Reject traversal/subdirectories and never evaluate paths.
if [[ -f "$MANIFEST" ]]; then
    if ci_run cp "$MANIFEST" "$CI_RUN_DIR/manifest-before-cleanup.txt"; then
        awk '$2 == "add" && !seen[$3]++ { print $3 }' "$CI_RUN_DIR/manifest-before-cleanup.txt" > "$CI_RUN_DIR/added-paths.txt" || ERRORS=$((ERRORS + 1))
        while IFS= read -r added; do
            case "$added" in
                "$CLUSTER_DIR"/*)
                    relative=${added#"$CLUSTER_DIR"/}
                    case "$relative" in ''|.|..|*/*) ci_log "MANIFEST_PATH_REJECTED $added" ;;
                        *) remove_added "$added" || ERRORS=$((ERRORS + 1)) ;;
                    esac ;;
                *) ci_log "MANIFEST_PATH_REJECTED $added" ;;
            esac
        done < "$CI_RUN_DIR/added-paths.txt"
    else
        ERRORS=$((ERRORS + 1))
    fi
fi
if [[ -d "$CLUSTER_DIR" && -z "$(ls -A "$CLUSTER_DIR")" ]]; then
    ci_run rmdir "$CLUSTER_DIR" || ERRORS=$((ERRORS + 1))
fi
ci_run sync || ERRORS=$((ERRORS + 1))
if [[ $ERRORS -ne 0 ]]; then fail "ROLLBACK_ERRORS=$ERRORS"; exit 6; fi
if [[ $CI_SELFTEST -eq 1 ]]; then
    same "$APPS_DIR/gal.real" "$APPS_DIR/gal" || exit 6
    same "$APPS_DIR/dio_manager.real" "$APPS_DIR/dio_manager" || exit 6
    [[ ! -e "$CLUSTER_DIR/cluster" && ! -e "$JAR_DIR/ClusterIntegration_selftest.jar" ]] || exit 6
    [[ -f "$JAR_DIR/NaviCompass.jar" && -f "$JAR_DIR/AppDevelopment.jar" ]] || exit 6
    CI_RESULT=SELFTEST_OK
else
    CI_RESULT=ROLLBACK_OK
fi
ci_log 'UNINSTALL_COMPLETE no automatic reboot; restart MMI normally to reload Java'
exit 0
