#!/bin/ksh
#
# Copyright (c) 2026 fifthBro
# https://fifthbro.github.io
#
# Licensed under CC BY-NC-SA 4.0
# https://creativecommons.org/licenses/by-nc-sa/4.0/
# NOT FOR COMMERCIAL USE
#
# failsafe.sh - unattended rollback for a head unit that no longer boots
# cleanly after installing this mod.
#
# The MH2p ModKit runs a file called failsafe.sh from the ROOT of the SD card
# or USB stick early in every boot (before any mod's Persist step). This file
# is shipped inside Mods/ClusterIntegration/ so that it does NOT run by
# itself. To use it:
#
#   1. copy Mods/ClusterIntegration/failsafe.sh to the root of the SD card
#   2. insert the card and switch the ignition on
#   3. wait for the unit to come up, then remove the card and delete the
#      root-level failsafe.sh again (otherwise it rolls back on every boot)
#
# It runs uninstall.sh with MOD_PATH pointing at the Update folder on this
# card, and appends its output to Logs/ClusterIntegration-failsafe.log.

MEDIA="${0%/*}"
[[ "$MEDIA" == "$0" ]] && MEDIA="."
export MOD_PATH="$MEDIA/Mods/ClusterIntegration/Update"
mkdir -p "$MEDIA/Logs" 2>/dev/null
LOG="$MEDIA/Logs/ClusterIntegration-failsafe.log"

if [[ ! -f "$MOD_PATH/uninstall.sh" ]]; then
    echo "failsafe: $MOD_PATH/uninstall.sh not found" >> "$LOG"
    exit 1
fi
# (echo, not print: ksh's print would parse the leading dashes as options)
echo "----- failsafe rollback $(date) -----" >> "$LOG"
/bin/ksh "$MOD_PATH/uninstall.sh" >> "$LOG" 2>&1
rc=$?
echo "----- failsafe done rc=$rc -----" >> "$LOG"
exit $rc
