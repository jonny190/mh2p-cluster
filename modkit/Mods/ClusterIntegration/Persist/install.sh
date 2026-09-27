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

if [[ -x "/eso/bin/apps/cluster/cluster" ]]; then
        /eso/bin/apps/cluster/cluster daemon verbose=2 &
fi

