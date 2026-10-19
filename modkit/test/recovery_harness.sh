#!/usr/bin/env bash
# Host-only ksh/OS adapters. Production --self-test never calls mount or slay.
set -u
shopt -s extglob
print() {
    local fd=1
    [[ ${1:-} != -u2 ]] || { fd=2; shift; }
    [[ ${1:-} != -r ]] || shift
    [[ ${1:-} != -- ]] || shift
    printf '%s\n' "$*" >&"$fd"
}
whence() {
    case ${RECOVERY_FAULT:-} in
        no_compare|bundle_*) case "$1" in cmp|sha256sum|md5sum|diff) return 1 ;; esac ;;
        hash_only) case "$1" in cmp|diff) return 1 ;; esac ;;
    esac
    command -v "$1"
}
date() { [[ $# -eq 0 ]] || return 8; echo 'Thu Jan 1 00:00:00 UTC 1970'; }
mount() { echo FORBIDDEN_HOST_MOUNT >&2; return 90; }
slay() { echo FORBIDDEN_HOST_SIGNAL >&2; return 90; }
dd() {
    if [[ ${RECOVERY_FAULT:-} == header_read ]]; then
        for arg in "$@"; do [[ $arg != of=*elf-header-* ]] || return 9; done
    fi
    command dd "$@"
}
cp() {
    local dst=${@: -1}
    case ${RECOVERY_FAULT:-} in
        backup) [[ $dst != *gal.removed.* ]] || return 9 ;;
        stage) [[ $dst != *gal.mib2p-restore-stage ]] || return 9 ;;
        stage_dio) [[ $dst != *dio_manager.mib2p-restore-stage ]] || return 9 ;;
        helper_backup) [[ $dst != *cluster.removed.* ]] || return 9 ;;
    esac
    command cp "$@" || return $?
    case ${RECOVERY_FAULT:-} in
        bad_original) [[ $dst != */sandbox-root/apps/gal.real ]] || printf BAD > "$dst" ;;
        short_original) [[ $dst != */sandbox-root/apps/gal.real ]] || printf '\177EL' > "$dst" ;;
        corrupt_backup) [[ $dst != *gal.removed.* ]] || printf BAD > "$dst" ;;
        corrupt_stage) [[ $dst != *gal.mib2p-restore-stage ]] || printf BAD > "$dst" ;;
    esac
    return 0
}
chmod() { [[ ${RECOVERY_FAULT:-} != chmod_stage || ${@: -1} != *.mib2p-restore-stage ]] || return 9; command chmod "$@"; }
mv() { [[ ${RECOVERY_FAULT:-} != rename ]] || return 9; command mv "$@"; }
rm() { [[ ${RECOVERY_FAULT:-} != jar_remove || ${@: -1} != *ClusterIntegration_selftest.jar ]] || return 9; command rm "$@"; }
sync() { [[ ${RECOVERY_FAULT:-} != sync ]] || return 9; }
export MOD_PATH=$1
unset modPath
shift
source "$MOD_PATH/uninstall.sh" "$@"
