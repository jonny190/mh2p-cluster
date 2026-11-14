#!/bin/ksh
# altscreen (CarPlay instrument-cluster video): opens TCP 6031 in Audi's packet
# filter. Installed as cluster/altscreen_pf.sh with enable_carplay_altscreen.txt
# and started in the background at every boot by Persist/install.sh.
# GPL-3.0-or-later, see src/altscreen/altscreen.c.
#
# The iPhone sends the cluster stream to TCP 6031 (the dio_manager hook's
# port next to the main screen's 6030). Audi's packet filter passes only a
# fixed port list on the hotspot interfaces bcm0/bcm1, so the 6030 rules are
# copied for 6031 and only the filter part is reloaded (pfctl -R, atomic: a
# failed load changes nothing). The system loads its rules again when the
# hotspot comes up (seen 30-60 s after boot on an A6 C8), right when the
# iPhone connects, so the loop checks every 2 s for the first 5 minutes and
# every 30 s after that. One line per change goes to
# /mnt/ota/modkit/Logs/altscreen-pf.log.
export PATH="$PATH:/mnt/app/armle/sbin:/mnt/app/armle/bin:/mnt/app/armle/usr/bin:/sbin:/usr/sbin"
LOG=/mnt/ota/modkit/Logs/altscreen-pf.log
fails=0

apply_pf() {
    pfctl -sr 2>/dev/null | grep -q "port = 6031" && return 0
    pfctl -sr > /tmp/altscreen_pf_before.txt 2>/dev/null
    grep -q "port = 6030 keep state" /tmp/altscreen_pf_before.txt || return 2
    sed -e "/port = 6030 keep state/p" \
        -e "s/port = 6030 keep state/port = 6031 keep state/" \
        /tmp/altscreen_pf_before.txt > /tmp/altscreen_pf_new.txt || return 2
    if pfctl -R -f /tmp/altscreen_pf_new.txt > /tmp/altscreen_pf_load.txt 2>&1; then
        echo "$(date) pf: 6031 opened"
        return 0
    fi
    echo "$(date) pf: reload failed"
    cat /tmp/altscreen_pf_load.txt
    return 2
}

check() {
    if apply_pf >> /tmp/altscreen_pf_last.txt 2>&1; then
        cat /tmp/altscreen_pf_last.txt >> "$LOG" 2>/dev/null
    else
        fails=$((fails + 1))
        # a filter that never takes the rule must not fill the flash log
        [[ $fails -le 20 ]] && cat /tmp/altscreen_pf_last.txt >> "$LOG" 2>/dev/null
    fi
    rm -f /tmp/altscreen_pf_last.txt
}

mkdir -p /mnt/ota/modkit/Logs 2>/dev/null
echo "=== boot $(date)" >> "$LOG"
waitfor /tmp/pf_ready 30 2>/dev/null
sleep 10
check
( n=0; while true; do
      if [[ $n -lt 150 ]]; then sleep 2; n=$((n + 1)); else sleep 30; fi
      check
  done ) &
echo "altscreen: pf watcher started"
