#!/bin/ksh
#
# Copyright (c) 2026 fifthBro
# https://fifthbro.github.io
#
# Licensed under CC BY-NC-SA 4.0
# https://creativecommons.org/licenses/by-nc-sa/4.0/
# NOT FOR COMMERCIAL USE
#
# cluster.sh
#

# The gal wrapper starts the daemon too when AA comes up before this runs.
# Two daemons race for /tmp/cluster_ctl and drop commands (blank cockpit until
# a reconnect, native map not restored after unplug), so only one may run.
# Whoever starts it records its pid in /tmp/cluster_daemon.pid. The check uses
# shell built-ins only (read, test): pidin/grep are not reliably reachable this
# early in boot, and kill -0 fails across users, but /proc/<pid> is visible to
# everyone. One line per decision goes to /tmp/cluster_daemon_start.log.
DAEMON_PID=/tmp/cluster_daemon.pid
DAEMON_LOG=/tmp/cluster_daemon_start.log
if [[ -x "/eso/bin/apps/cluster/cluster" ]]; then
        if { read p < "$DAEMON_PID" && [[ -n "$p" && -d "/proc/$p" ]]; } 2>/dev/null; then
                print "persist: daemon already running, pid $p" >> "$DAEMON_LOG" 2>/dev/null
        else
                # verbose=2 (cluster_daemon.log) only with the marker the
                # installer sets from Mods/ClusterIntegration/daemon_log.txt
                V=""
                [[ -f /eso/bin/apps/cluster/daemon_verbose ]] && V="verbose=2"
                /eso/bin/apps/cluster/cluster daemon $V >/dev/null 2>&1 &
                p=$!
                print $p > "$DAEMON_PID" 2>/dev/null
                print "persist: started daemon pid $p (pid file written: $?)" >> "$DAEMON_LOG" 2>/dev/null
        fi
fi

# CarPlay cluster video (enable_carplay_altscreen.txt): the iPhone streams it
# to TCP 6031, which Audi's packet filter blocks. The script waits for the
# filter and then keeps the port open, so it runs in the background.
if [[ -x "/eso/bin/apps/cluster/altscreen_pf.sh" ]]; then
        /eso/bin/apps/cluster/altscreen_pf.sh >/dev/null 2>&1 &
        print "persist: started altscreen_pf.sh pid $!" >> "$DAEMON_LOG" 2>/dev/null
fi

