# Local verbose stock rollback

The installer and uninstaller write numbered directories under SD
`Logs/ClusterIntegration/`. Each holds `commands.log` (shell tracing, raw
date, utility locations, commands, results and byte verification) and
`result.txt`. The first console line gives the full log path. There is no
formatted date dependency; backup IDs are reserved with numbered directories.

`Update/rollback.sh --check` checks tools/originals without mounts, process
signals or changes to live software. `Update/rollback.sh --self-test` restores
the genuine saved GAL in a sandbox under its SD log directory and removes
dummy mod files. This does not touch live GAL, Java or NavCompass. A check
does not prove successful writes to the application filesystem or boot timing.

With no arguments, rollback stages and verifies originals before stopping
the affected native processes, renames verified stages into place, verifies
the installed bytes, and removes the added mod helpers/JARs only after making
verified pre-change copies. Stock JARs and NavCompass are retained. The fixed
allowlist includes `gal_env` and the Audi StorageMountHandler stub. It never
executes removal paths from a mutable manifest. Any critical failure returns
nonzero; a failed helper backup retains that helper and returns failure.
There is no automatic reboot. Restart the MMI normally to reload stock Java.

The prepared card supplies two independently written copies of the saved
P2873 GAL under `Update/Stock/`. Its pinned SHA256 is checked if the unit has
sha256sum; otherwise the copies are compared byte-for-byte/digest. On the PC,
verify the whole card using SHA256SUMS. If no working cmp, SHA256, MD5 or diff
is available on the unit, preflight blocks installation/rollback before live
changes; size or word counts are insufficient. Other originals fall back to
the unit's `.real` or installer-created SD `Backup/*.real`. Check output will
state the selected source. Original `.real` files are retained after rollback.
A differing unit `.real` is backed up and refreshed from the verified source,
so a future installation cannot accidentally reuse a damaged original.

The root `modkit/sd_failsafe.sh` dispatches a check by default. The existing
`Mods/ClusterIntegration/uninstall.txt` marker selects real rollback. Live
rollback also marks the installed `/mnt/ota/modkit/Mods/ClusterIntegration`
entry for uninstall, using the exact contract in the release's ModKit persist
script. No new arming-file convention is introduced. The original mod-level
`failsafe.sh` remains an unconditional rollback entry when copied to card root.
Only one script can occupy the root `failsafe.sh` name.

Host verification: `scripts/test_recovery.py --stock-gal <saved-gal>` checks
shell syntax, genuine-original restoration, backup/copy/corruption/rename and
removal failures, missing originals/tools, and concurrent numbered log creation.
Bash supplies host-only adapters for Korn-shell builtins. This is not a QNX
execution or proof of the early boot hook. No host test mounts or kills processes.
