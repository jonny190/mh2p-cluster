#!/bin/ksh
# CC BY-NC-SA 4.0. Local MIB2p logging helpers; see LICENSE.md.
# Sourced by installer/uninstaller. No formatted date, tar, tee or sleeps.
ci_have() { whence "$1" >/dev/null 2>&1; }
ci_log() {
    CI_EVENT=$((CI_EVENT + 1))
    print -r -- "EVENT=$CI_EVENT ROLE=$CI_ROLE PID=$$ $*"
}
ci_run() {
    ci_log "COMMAND $*"
    "$@"
    typeset command_rc=$?
    ci_log "COMMAND_RESULT rc=$command_rc $*"
    return "$command_rc"
}
ci_stamp() {
    # Command substitution discards variables: allocate IDs on disk.
    typeset n=1
    while [[ $n -le 10000 ]]; do
        if mkdir "$CI_RUN_DIR/stamp-$n" 2>/dev/null; then
            print -r -- "$CI_RUN_ID-$n"
            return 0
        fi
        [[ -d "$CI_RUN_DIR/stamp-$n" ]] || return 1
        n=$((n + 1))
    done
    return 1
}
ci_finish() {
    typeset final_rc=$1
    set +x
    ci_log "EXIT_STATUS=$final_rc RESULT=$CI_RESULT"
    print -r -- "EXIT_STATUS=$final_rc RESULT=$CI_RESULT LOG=$CI_LOG" > "$CI_RUN_DIR/result.txt" || final_rc=1
    exit "$final_rc"
}
ci_log_init() {
    CI_ROLE=$1
    CI_EVENT=0
    CI_RESULT=INCOMPLETE
    typeset log_root="$MOD_PATH/../../../Logs/ClusterIntegration"
    mkdir -p "$log_root" || return 1
    typeset n=1
    while [[ $n -le 10000 ]]; do
        CI_RUN_ID="$CI_ROLE-$n"
        CI_RUN_DIR="$log_root/$CI_RUN_ID"
        if mkdir "$CI_RUN_DIR" 2>/dev/null; then break; fi
        [[ -d "$CI_RUN_DIR" ]] || return 1
        n=$((n + 1))
    done
    [[ $n -le 10000 ]] || return 1
    CI_RUN_DIR="$(cd "$CI_RUN_DIR" && pwd)" || return 1
    CI_LOG="$CI_RUN_DIR/commands.log"
    print -r -- "ClusterIntegration $CI_ROLE log: $CI_LOG"
    exec >> "$CI_LOG" 2>&1
    trap 'ci_finish $?' 0
    ci_log "ENTRY parent=${PPID:-unknown} mod_path=$MOD_PATH"
    ci_log "PATH=${PATH:-unset}"
    ci_have date && ci_run date
    ci_have uname && ci_run uname -a
    for utility in cp mv rm mkdir chmod cmp diff sha256sum md5sum mount slay sync pidin; do
        ci_log "UTILITY $utility"
        whence "$utility" || :
    done
    PS4='+ role=$CI_ROLE pid=$$ line=${LINENO:-unknown} '
    set -x
    return 0
}
