#!/bin/ksh
# CC BY-NC-SA 4.0. Prepared SD startup probe/rollback dispatcher.
# Default: check only. Existing ModKit uninstall.txt marker selects rollback.
MEDIA="${0%/*}"
[[ "$MEDIA" == "$0" ]] && MEDIA=.
MEDIA="$(cd "$MEDIA" && pwd)" || exit 1
export MOD_PATH="$MEDIA/Mods/ClusterIntegration/Update"
unset modPath
mkdir -p "$MEDIA/Logs" || exit 1
LOG="$MEDIA/Logs/ClusterIntegration-failsafe.log"
echo "ENTRY pid=$$ parent=${PPID:-unknown} media=$MEDIA argc=$# raw_date=$(date 2>/dev/null)" >> "$LOG"
if [[ ! -f "$MOD_PATH/uninstall.sh" ]]; then
    echo "ERROR uninstaller missing" >> "$LOG"
    exit 1
fi
if [[ -f "$MEDIA/Mods/ClusterIntegration/uninstall.txt" ]]; then
    echo "MODE=rollback existing uninstall.txt marker" >> "$LOG"
    /bin/ksh "$MOD_PATH/uninstall.sh" >> "$LOG" 2>&1
else
    echo "MODE=check live files untouched" >> "$LOG"
    /bin/ksh "$MOD_PATH/uninstall.sh" --check >> "$LOG" 2>&1
fi
rc=$?
echo "EXIT_STATUS=$rc" >> "$LOG"
exit "$rc"
