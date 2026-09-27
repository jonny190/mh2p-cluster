#!/bin/ksh
#
# Copyright (c) 2026 fifthBro
# https://fifthbro.github.io
#
# Licensed under CC BY-NC-SA 4.0
# https://creativecommons.org/licenses/by-nc-sa/4.0/
# NOT FOR COMMERCIAL USE
#
# rollback.sh
#
# Convenience entry point for rolling the mod back over SSH, without going
# through the ModKit update flow:
#
#   ksh /fs/sda0/Mods/ClusterIntegration/Update/rollback.sh
#
# It runs uninstall.sh with MOD_PATH set to this folder. uninstall.sh restores
# gal and dio_manager from their .real originals (or from Backup/ on the SD
# card if .real is gone), removes the JAR and the cluster daemon files, and
# records what it did in Backup/manifest.txt.
#
# The ModKit equivalent is: create Mods/ClusterIntegration/uninstall.txt on
# the SD card and boot with the card inserted.

HERE="${0%/*}"
[[ "$HERE" == "$0" ]] && HERE="."
export MOD_PATH="$(cd "$HERE" && pwd)"
exec /bin/ksh "$MOD_PATH/uninstall.sh"
