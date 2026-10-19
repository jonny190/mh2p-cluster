# Verified uninstall and numbered recovery logs

This is the shell-only recovery split requested in PR #12. It does not require
a native rebuild, private firmware files or lifecycle mode. Existing Java,
native binaries, profiles and release ZIPs are unchanged.

With a writable SD card mounted at `/fs/sdb0`, from the MMI shell:

```sh
export MOD_PATH=/fs/sdb0/Mods/ClusterIntegration/Update
ksh "$MOD_PATH/uninstall.sh" --check
ksh "$MOD_PATH/uninstall.sh" --self-test
```

`--check` inspects the selected originals/tools and writes only SD logs and
comparison/header probes. It does not remount, signal processes or alter live
application files. `--self-test` restores synthetic, non-executable fixtures in
an isolated directory under the SD log; it never runs those fixtures or touches
the MMI programs. Neither option proves real writes, native process exit or
physical display restoration. These scripts are host-tested; QNX testing is
still needed for this standalone upstream split.

For actual removal use the existing ModKit uninstall flow, or explicitly run:

```sh
ksh "$MOD_PATH/uninstall.sh"
```

The no-argument command still performs a real uninstall. It validates original
ELF magic and stages/verifies both executable replacements before signalling
processes. It copies and renames verified stages over the wrappers, retains the
last `.real` originals, then removes known mod files/JAR name patterns
after verified pre-change backups. It also backs up/removes manifest-listed additions only when they are immediate
children of the mod directory; traversal and other paths are rejected. A critical backup, staging, rename, comparison or
removal failure returns nonzero; helpers stay when restoration fails. Backups
are not consumed by restoration. It does not reboot; restart MMI normally to
reload stock Java after successful removal.

Original selection is generic: first the unit's `NAME.real`, otherwise
`Update/Backup/NAME.real` when the active file is a wrapper. This does not prove
that an original is unmodified OEM firmware, and does not reverse earlier
third-party binary patches. ELF magic detects a wrong header, not full ABI or
firmware identity. No P2873 hashes or stock firmware binaries are shipped.

Logs are allocated using `mkdir` as `Logs/ClusterIntegration/rollback-N`.
The first console line prints `commands.log`; `result.txt` records the exit and
completion result. Allocation and backup names require no formatted date; a
1970 clock is fine, and concurrent sessions receive separate directories.
Raw commands/results, utility availability, comparison results and shell trace
are retained. Logs can contain paths and platform details; inspect before sharing.

Comparison must pass equal, different, truncated and missing-file tests. The
selector tries `cmp`, SHA256, MD5 and `diff`; size/line counts are never accepted.
If a unit lacks those tools, a separately supplied `Update/qnx_file_equal` may
be used after the same interface tests plus full-byte-range fixtures. Its exit
contract is 0 identical, 1 different, 2 I/O error. No native comparator binary is
bundled in this PR; do not proceed if neither a system tool nor a trusted,
compatible optional helper passes preflight. `elf-magic.bin`, `selftest-original.bin` and equal fixtures
are tiny test data, not firmware executables.

The standard package builder already overlays the complete `modkit/Mods` tree,
so it includes the helper scripts/fixtures. Existing packaged release archives
are not silently rewritten by this change; rebuild them through the established
package workflow to ship the new uninstall code. Existing failsafe dispatch and
installer code are unchanged; making installation depend on `--check`, or
changing failsafe default behavior, can be reviewed separately.

Host tests (Bash + Python3; no QNX SDK or firmware needed):

```sh
python3 modkit/test/test_recovery.py
```

On Windows pass `--bash 'C:/Program Files/Git/bin/bash.exe'`. The harness supplies
host-only ksh builtins, rejects mount/signal attempts, injects filesystem faults
and checks that unrelated JARs and originals survive. It is never packaged into
the car's update directory.
