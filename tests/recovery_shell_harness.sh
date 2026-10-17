#!/usr/bin/env bash
# Host-only Korn-shell builtins/platform adapter. Never used on the car.
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
    if [[ ${RECOVERY_FAULT:-} == no_compare ]]; then
        case "$1" in cmp|sha256sum|md5sum|diff) return 1 ;; esac
    fi
    command -v "$1"
}
date() {
    [[ $# -eq 0 ]] || { echo 'formatted date unsupported' >&2; return 8; }
    echo 'Thu Jan 1 00:00:00 UTC 1970'
}
mount() { echo 'FORBIDDEN_HOST_MOUNT' >&2; return 90; }
slay() { echo 'FORBIDDEN_HOST_SIGNAL' >&2; return 90; }
cp() {
    local dst=${@: -1}
    case ${RECOVERY_FAULT:-} in
        backup) [[ $dst != *gal.removed.* ]] || return 9 ;;
        stage) [[ $dst != *.mib2p-restore-stage ]] || return 9 ;;
        helper_backup) [[ $dst != *cluster.removed.* ]] || return 9 ;;
    esac
    command cp "$@" || return $?
    if [[ ${RECOVERY_FAULT:-} == bad_unit_original && $dst == */sandbox-root/apps/gal.real ]]; then
        printf '\177ELFcorrupt unit original\n' > "$dst"
    fi
    if [[ ${RECOVERY_FAULT:-} == corrupt_stage && $dst == *.mib2p-restore-stage ]]; then
        printf 'bad executable\n' > "$dst"
    fi
}
mv() {
    [[ ${RECOVERY_FAULT:-} != rename ]] || return 9
    command mv "$@"
}
rm() {
    if [[ ${RECOVERY_FAULT:-} == jar_remove && ${@: -1} == *ClusterIntegration_selftest.jar ]]; then return 9; fi
    command rm "$@"
}
export MOD_PATH=$1
unset modPath
shift
source "$MOD_PATH/uninstall.sh" "$@"
