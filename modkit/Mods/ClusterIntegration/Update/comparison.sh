#!/bin/ksh
# CC BY-NC-SA 4.0. Strong comparison only; no size/word-count fallback.
ci_compare_with() {
    typeset kind=$1 a=$2 b=$3 ha hb
    case "$kind" in
        cmp) cmp -s "$a" "$b" ;;
        diff) diff "$a" "$b" >/dev/null 2>&1 ;;
        sha256sum|md5sum)
            ha=$("$kind" < "$a") || return 2
            hb=$("$kind" < "$b") || return 2
            [[ -n "$ha" && -n "$hb" ]] || return 2
            [[ "$ha" == "$hb" ]] ;;
        bundled) "$CI_COMPARE_BINARY" "$a" "$b" ;;
        *) return 2 ;;
    esac
}
ci_compare_probe() {
    typeset kind=$1 a=$2 b=$3 short=$4 missing=$5
    ci_compare_with "$kind" "$a" "$a" || return 1
    ci_compare_with "$kind" "$a" "$b"
    [[ $? -eq 1 ]] || return 1
    ci_compare_with "$kind" "$a" "$short"
    [[ $? -eq 1 ]] || return 1
    ci_compare_with "$kind" "$a" "$missing"
    [[ $? -eq 2 ]] || return 1
    return 0
}
ci_compare_init() {
    typeset dir="$CI_RUN_DIR/compare-probe" tool a b short
    CI_COMPARE_TOOL=""
    CI_COMPARE_BINARY="$MOD_PATH/qnx_file_equal"
    ci_run mkdir "$dir" || return 1
    a="$dir/a"; b="$dir/b"; short="$dir/short"
    print -r -- 'probe-A' > "$a" || return 1
    print -r -- 'probe-B' > "$b" || return 1
    ci_run dd if="$a" of="$short" bs=1 count=6 || return 1
    for tool in cmp sha256sum md5sum diff; do
        ci_have "$tool" || continue
        if ci_compare_probe "$tool" "$a" "$b" "$short" "$dir/missing"; then
            CI_COMPARE_TOOL=$tool
            break
        fi
    done
    if [[ -z "$CI_COMPARE_TOOL" && -f "$CI_COMPARE_BINARY" ]]; then
        # FAT may not support chmod although execution is already allowed.
        ci_run chmod 755 "$CI_COMPARE_BINARY" || ci_log "COMPARATOR_CHMOD_FAILED checking execution directly"
        [[ -x "$CI_COMPARE_BINARY" ]] || return 1
        if ci_compare_probe bundled "$a" "$b" "$short" "$dir/missing"; then
            [[ -f "$MOD_PATH/equal-a.bin" && -f "$MOD_PATH/equal-b.bin" ]] || return 1
            ci_compare_with bundled "$MOD_PATH/equal-a.bin" "$MOD_PATH/equal-a.bin" || return 1
            ci_compare_with bundled "$MOD_PATH/equal-a.bin" "$MOD_PATH/equal-b.bin"
            [[ $? -eq 1 ]] || return 1
            ci_compare_with bundled "$MOD_PATH/equal-a.bin" "$dir/missing"
            [[ $? -eq 2 ]] || return 1
            CI_COMPARE_TOOL=bundled
        fi
    fi
    [[ -n "$CI_COMPARE_TOOL" ]] || return 1
    ci_log "COMPARISON_TOOL=$CI_COMPARE_TOOL binary=$CI_COMPARE_BINARY selftests=PASS"
    return 0
}
ci_compare() { ci_compare_with "$CI_COMPARE_TOOL" "$1" "$2"; }
