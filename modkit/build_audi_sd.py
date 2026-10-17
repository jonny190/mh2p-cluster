"""Prepare a private SD copy of the pinned upstream Audi release.

Only unsigned Mods/root recovery files change. Data and Meta bytes are
preserved. Native/Java release binaries remain upstream, not our local port.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import zipfile
from port_build import JAR as PORT_JAR, validate_artifacts, fingerprint

ROOT = Path(__file__).resolve().parents[1]
RELEASE_SHA = 'ee0ed7af7f96230e0181d9bd640b607c6520b31b6124267ea70a6decee8c59d7'
STOCK_SHA = '9d455b764854f7977971ae977b21fe7560d8a40cca86d31cb9c9a7f5821c0ecf'
UPD = 'Mods/ClusterIntegration/Update/'
GUIDE = '''# Audi MIB2p SD package — first diagnostic test

Prepared for the saved MH2p_ER_AUG35_P2873 firmware. This is the runnable
upstream v0034 Audi release with our verbose stock rollback scripts. It
DOES NOT contain our locally developed disconnect/reconnect or generation
changes: those need a matched ARM/QNX native and Java build. No replacement
Windows test executable is shipped. The first diagnostic pass installs the
mod's AA-only Java classes and GAL hook but disables cluster video injection
and map rendering. It is a real software modification, not passive logging.
The upstream e-tron report does not establish A6 compatibility.

## Copy to SD

Extract the ZIP on the PC first. Use an empty FAT32 SD card, unlocked.
For the FIRST recovery checks, copy everything EXCEPT Data/ and Meta/ to
the card root. Mods/, Recovery/, failsafe.sh, SHA256SUMS, VERIFY-CARD.ps1
and this guide go directly at the root. Do not add a root
cluster_config.json or unrelated mods. Keep the extracted PC copy.

IMPORTANT: a complete ModKit update card can start installation
automatically when inserted. Leave BOTH Data/ and Meta/ off the card
until the recovery checks below pass. Renaming them on the card is not
the procedure; keep them on the PC. This preflight card relies on your
already installed ModKit startup hook and does not reinstall ModKit.

Verify the preflight card in Windows PowerShell:

    powershell -NoProfile -ExecutionPolicy Bypass -File X:\\VERIFY-CARD.ps1 -PreflightOnly

Replace X: with the actual card drive. Expected: 38 files verified.
The verifier also rejects Data/ or Meta/ on a preflight card.
After the checks pass and you add Data/ and Meta/ for installation, run:

    powershell -NoProfile -ExecutionPolicy Bypass -File X:\\VERIFY-CARD.ps1

Expected: 53 files verified. Run verification before changing marker
files or configuration. SHA256SUMS does not include itself or logs.

## Check recovery before installing

The supplied root failsafe.sh defaults to CHECK ONLY. With the existing
ModKit startup hook and this card inserted, a normal MMI restart should
produce Logs/ClusterIntegration-failsafe.log and numbered rollback logs.
The hook is started alongside normal services, so this does not prove it
finishes before Java starts. Do not deliberately break the MMI to test it.

Park the car; keep it powered for the test. Insert the PREFLIGHT card,
then use your usual normal MMI restart to test the installed startup hook.
Do not use the full update card for this check.

Connect from Windows PowerShell (retain any options/key from your working
connection if it needs them):

    ssh -p 2012 root@172.16.250.248

The following commands run INSIDE the MMI SSH shell, not PowerShell.
Set the session PATH and locate the card without guessing its mount:

    export PATH=/proc/boot:/bin:/usr/bin:/sbin:/usr/sbin:/mnt/app/armle/bin:/mnt/app/armle/usr/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/sbin:$PATH
    CARD=
    for p in /fs/sda0 /fs/sdb0 /fs/usb0_0; do
        if [ -f "$p/Mods/ClusterIntegration/Update/rollback.sh" ]; then
            if [ -n "$CARD" ]; then echo "More than one test card; remove the extra card"; CARD=; break; fi
            CARD="$p"
        fi
    done
    echo "CARD=$CARD"

Do not run the next commands unless CARD prints one actual mount path:

    ksh "$CARD/Mods/ClusterIntegration/Update/rollback.sh" --check
    echo "CHECK_EXIT=$?"
    ksh "$CARD/Mods/ClusterIntegration/Update/rollback.sh" --self-test
    echo "SELFTEST_EXIT=$?"
    cat "$CARD/Logs/ClusterIntegration"/rollback-*/result.txt
    cat "$CARD/Logs/ClusterIntegration-failsafe.log"

The card can instead be /fs/sdb0 or /fs/usb0_0. --self-test uses your actual
saved GAL bytes in an isolated directory on the SD and removes dummy mod
files there. It never mounts the app filesystem or signals live processes.
Expect CHECK_OK / SELFTEST_OK and EXIT_STATUS=0 in the numbered result.txt,
and MODE=check / EXIT_STATUS=0 in the startup failsafe log from this boot.
Missing startup logs mean the automatic recovery path has not been shown
to run. Stop before installing if any check fails or these logs are missing.
No working byte/digest comparison tool means installation is blocked.
Resolve that tool limitation before installing; do not bypass it.

## Diagnostic installation

diag.txt, config_overrides.txt and the packaged config disable map video.
gal_env.txt enables native logs without overriding diagnostic MERGE=0.
The AA-only JAR and Audi dependency stub are included; CarPlay is not opted
in and there is no force_install.txt. The native/Java binaries are preserved
from the pinned upstream release. Our receiver generation patch is absent.

Eject the preflight card and save its Logs/ on the PC. Copy the unchanged
Data/ and Meta/ folders from the extracted package to its root, and verify
all 53 files without -PreflightOnly. Do not add any enable/force markers.

With the car parked and running safely outdoors, insert the full card.
ModKit normally starts automatically within about 10 seconds. Allow the
update to finish without touching the MMI or removing media; multiple
reboots and engineering screens are expected. Remove the card ONLY when
it says "Please remove update media", then let normal startup finish.
If the update does not start or shows an error, retain the screen/error and
logs; do not force installation or guess a manual engineering-menu update.
Official procedure: https://lawpaul.github.io/MH2p_SD_ModKit_Site/

An installer log is created under
Logs/ClusterIntegration/install-N/commands.log and rollback preflight has
its own rollback-N log. Each log records commands/branches and stderr;
result.txt contains the exit status. INSTALL_SCRIPT_COMPLETE means this
script finished, not that AA or cockpit rendering has passed a vehicle test.

Afterwards check normal MMI, Android Auto, stock maps and NavCompass first.
Connect the phone and request Google Maps navigation while parked. Record
whether AA starts, turn arrows work, and normal maps/NavCompass still work.
The FIRST pass intentionally does not show the AA map video on the cockpit.
Record what happens when navigation stops and the phone disconnects; if
normal functionality does not return, stop and use rollback below.

Save the card Logs/ on the PC. BEFORE reinserting the card for runtime logs
or recovery, move Data/ and Meta/ back OFF the card to the PC, then verify
with -PreflightOnly. This prevents a second automatic update. Reconnect SSH,
locate CARD again and save temporary runtime logs before restarting:

    for f in /tmp/gal_preload.log /tmp/gal_cluster.log /tmp/cluster.log /tmp/cluster_hashed.log; do
        if [ -f "$f" ]; then cp "$f" "$CARD/Logs/"; fi
    done
    sync

Also retain any cluster.log written directly to the card.
Do not enable video until the diagnostic results have been reviewed.

## Restore stock functionality

If SSH works:

    ksh /fs/sda0/Mods/ClusterIntegration/Update/rollback.sh

For startup recovery, create an empty file at
Mods/ClusterIntegration/uninstall.txt on this card, insert it and perform
a normal MMI restart. Keep Data/ and Meta/ OFF the recovery card so this
uses the installed startup hook without launching another update.
The root failsafe sees that existing ModKit marker
and invokes rollback rather than a check. The same marker also selects
uninstall when running the ModKit update flow. No separate arm file is used.

Recovery restores GAL from the saved stock bytes supplied on this card,
verifies the installed copy, and removes this mod's added JARs/helpers after
verified pre-change backups. Stock JARs and NavCompass are retained. The
installed ClusterIntegration Persist entry is marked for uninstall, and
the card remains in uninstall mode to prevent accidental reinstallation.
There is no automatic reboot; restart the MMI normally to reload stock
Java classes after rollback. Recovery failures are logged and return nonzero.
Retain the logs if it does not finish. Recovery runs only if QNX mounts the
card and executes the installed hook, or if you invoke it over SSH/ModKit.
It cannot recover a unit which never reaches that execution path.

Detailed script behaviour: Recovery/RECOVERY_LOGGING.md.
Private saved GAL bytes are included. Do not publish this card package.
'''
VERIFY_PS = r'''param([switch]$PreflightOnly)
$ErrorActionPreference = 'Stop'
$cardRoot = $PSScriptRoot
$cardPrefix = [IO.Path]::GetFullPath($cardRoot).TrimEnd([char]92) + [IO.Path]::DirectorySeparatorChar
if ($PreflightOnly) {
    foreach ($updateFolder in @('Data', 'Meta')) {
        if (Test-Path -LiteralPath (Join-Path $cardRoot $updateFolder)) {
            throw "Preflight card must not contain $updateFolder; leave it on the PC until recovery checks pass."
        }
    }
}
$count = 0
foreach ($row in Get-Content -LiteralPath (Join-Path $cardRoot 'SHA256SUMS')) {
    if ($row -notmatch '^([0-9a-f]{64})  (.+)$') { throw 'Invalid checksum row' }
    $expectedHash = $Matches[1]
    $relativeName = $Matches[2]
    if ($PreflightOnly -and $relativeName -match '^(Data|Meta)/') { continue }
    $targetFile = [IO.Path]::GetFullPath((Join-Path $cardRoot $relativeName))
    if (-not $targetFile.StartsWith($cardPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe checksum path' }
    $actualHash = (Get-FileHash -LiteralPath $targetFile -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) { throw "Checksum mismatch: $relativeName" }
    $count++
}
Write-Output "PASS: $count SD files verified."
'''


def sha(data):
    return hashlib.sha256(data).hexdigest()


def safe_name(raw):
    name = raw.replace('\\', '/')
    path = PurePosixPath(name)
    if path.is_absolute() or '..' in path.parts or ':' in name:
        raise ValueError(f'Unsafe ZIP entry: {raw}')
    return name


def validate_payload(payload, release, stock, port=None):
    protected = [name for name in release if name.startswith(('Data/', 'Meta/'))]
    for name in protected:
        assert payload[name] == release[name], f'Signed ModKit bytes changed: {name}'
    binaries = [name for name in release if name.endswith(('.jar', '.so')) or name == UPD + 'cluster']
    if port is not None:
        replaced = {UPD + 'cluster', UPD + 'gal_cluster.so'}
        replaced.update(n for n in binaries if n.endswith('.jar') and n.startswith(UPD) and
                        (n[len(UPD):].startswith('aa_only/') or '/' not in n[len(UPD):]))
        binaries = [n for n in binaries if n not in replaced]
    for name in binaries:
        assert payload[name] == release[name], f'Upstream binary changed: {name}'
    assert payload[UPD + 'Stock/gal.real'] == payload[UPD + 'Stock/gal.reference'] == stock
    config = json.loads(payload[UPD + 'cluster_config.json'])
    assert config['config']['enableMapRender'] is False
    assert 'Mods/ClusterIntegration/diag.txt' in payload
    for forbidden in ('Mods/ClusterIntegration/uninstall.txt', 'Mods/ClusterIntegration/enable_carplay.txt',
                      'Mods/ClusterIntegration/force_install.txt', 'cluster_config.json'):
        assert forbidden not in payload, forbidden
    aa_names = [n for n in payload if n.startswith(UPD + 'aa_only/') and n.endswith('.jar')]
    assert len(aa_names) == 1
    import io
    with zipfile.ZipFile(io.BytesIO(payload[aa_names[0]])) as jar:
        assert not any(n.startswith('de/audi/app/terminalmode/smartphone/carplay/') for n in jar.namelist())
        assert any(n.endswith('/AndroidAuto2EventListener.class') for n in jar.namelist())
        if port is not None:
            assert any(n.endswith('/ClusterLifecycle.class') for n in jar.namelist())
            assert any(n.endswith('/ClusterLifecycleFiles.class') for n in jar.namelist())
    if port is not None:
        assert payload[UPD + 'cluster'] == port['cluster']
        assert payload[UPD + 'gal_cluster.so'] == port['gal_cluster.so']
        assert payload[aa_names[0]] == port[PORT_JAR]
        assert config['config']['enableLifecyclePort'] is True
        assert b'GAL_CLUSTER_LIFECYCLE=1\n' in payload['Mods/ClusterIntegration/gal_env.txt']
    for name, data in payload.items():
        if name.endswith('.sh'):
            assert b'\r\n' not in data, name
    return dict(signed_files_unchanged=len(protected), upstream_binaries_unchanged=len(binaries),
                diagnostic_mode=True, contains_local_lifecycle_changes=port is not None, car_tested=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--release', type=Path, default=ROOT / 'builds/ClusterIntegration_v0034_beta2_candidate_90d0b76_audi.zip')
    p.add_argument('--stock-gal', type=Path, required=True)
    p.add_argument('--output-directory', type=Path, required=True)
    p.add_argument('--port-artifacts', type=Path, help='Matched output of port_build.py java and native; required for our disconnect/reconnect changes')
    args = p.parse_args()
    # Validate before creating anything. A Java-only build is not a vehicle
    # package; reject it rather than silently retaining upstream native files.
    port = validate_artifacts(args.port_artifacts) if args.port_artifacts else None
    release_data = args.release.read_bytes()
    assert sha(release_data) == RELEASE_SHA, 'Wrong upstream release ZIP'
    stock = args.stock_gal.read_bytes()
    assert sha(stock) == STOCK_SHA and stock[:4] == b'\x7fELF', 'Wrong saved stock GAL'
    release = {}
    with zipfile.ZipFile(args.release) as z:
        for entry in z.infolist():
            if entry.is_dir():
                continue
            name = safe_name(entry.filename)
            assert name not in release, name
            release[name] = z.read(entry)
    payload = dict(release)
    for source in sorted((ROOT / 'modkit/Mods').rglob('*')):
        if source.is_file():
            name = 'Mods/' + source.relative_to(ROOT / 'modkit/Mods').as_posix()
            data = source.read_bytes()
            payload[name] = data.replace(b'\r\n', b'\n') if source.suffix == '.sh' else data
    config = json.loads(payload[UPD + 'cluster_config.json'])
    config['config']['enableMapRender'] = False
    payload[UPD + 'cluster_config.json'] = (json.dumps(config, indent=2) + '\n').encode()
    payload['Mods/ClusterIntegration/diag.txt'] = b'diagnostic first pass\n'
    payload['Mods/ClusterIntegration/config_overrides.txt'] = b'enableMapRender=false\n'
    payload['Mods/ClusterIntegration/gal_env.txt'] = b'GAL_CLUSTER_LOG=1\nGAL_CLUSTER_LOG_ALL=1\nGAL_CLUSTER_LOG_DETAILS=1\nGAL_CLUSTER_SD_LOG=1\n'
    if port is not None:
        for name in list(payload):
            if name.startswith(UPD) and name.endswith('.jar'):
                rel = name[len(UPD):]
                if rel.startswith('aa_only/') or '/' not in rel:
                    del payload[name]
        payload[UPD + PORT_JAR] = port[PORT_JAR]
        payload[UPD + 'aa_only/' + PORT_JAR] = port[PORT_JAR]
        for name in ('cluster', 'gal_cluster.so', 'cluster_config.json'):
            payload[UPD + name] = port[name]
        payload['Mods/ClusterIntegration/gal_env.txt'] += b'GAL_CLUSTER_LIFECYCLE=1\n'
        payload['Mods/ClusterIntegration/config_overrides.txt'] += b'enableLifecyclePort=true\n'
        payload['Recovery/java-build.json'] = port['java-build.json']
        payload['Recovery/native-build.json'] = port['native-build.json']
    payload['failsafe.sh'] = (ROOT / 'modkit/sd_failsafe.sh').read_bytes().replace(b'\r\n', b'\n')
    payload[UPD + 'Stock/gal.real'] = stock
    payload[UPD + 'Stock/gal.reference'] = stock
    payload['Recovery/RECOVERY_LOGGING.md'] = (ROOT / 'modkit/RECOVERY_LOGGING.md').read_bytes().replace(b'\r\n', b'\n')
    payload['Recovery/upstream-README.md'] = release['README.md']
    if port is None:
        guide = GUIDE
    else:
        guide = '''# Audi MIB2p lifecycle port — first diagnostic test

This package contains our rebuilt disconnect/reconnect Java lifecycle,
GAL receiver generation guards, v2 shared ring and leased native player.
The modified Java and both ARM/QNX binaries have matching source/build
receipts. It remains an untested vehicle candidate: compilation and host
tests do not prove Screen output, OEM hand-back or QNX J9 atomic rename.
The IMapClusterService restore route needs car verification; absence of
that service blocks a later takeover rather than forcing an OEM display.

The first pass enables the lifecycle implementation but disables map
rendering and video service injection. After diagnostic review, enable
rendering through configuration; never substitute upstream native binaries.
The verbose stock rollback and saved original GAL are included.

''' + GUIDE[GUIDE.index('## Copy to SD'):]
        guide = guide.replace('The native/Java binaries are preserved\nfrom the pinned upstream release. Our receiver generation patch is absent.',
                              'The native/Java lifecycle binaries are rebuilt together. The unchanged\nAudi dependency stub and ModKit update framework come from the pinned release.')
    payload['README.md'] = guide.encode('utf-8')
    payload['VERIFY-CARD.ps1'] = VERIFY_PS.encode('utf-8')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    receipt = dict(schema=1, kind=('local-lifecycle-diagnostic-with-stock-rollback' if port else 'upstream-audi-diagnostic-with-verbose-stock-rollback'),
                   prepared_date='2026-10-14', firmware='MH2p_ER_AUG35_P2873',
                   script_commit=commit, upstream_commit='ae1b8fe3a6a679f33dee9a93ab7adf43772c83d0',
                   release_sha256=RELEASE_SHA, saved_gal_sha256=STOCK_SHA,
                   **validate_payload(payload, release, stock, port))
    if port is not None:
        receipt['port_source_fingerprint'] = fingerprint()
    payload['Recovery/package-provenance.json'] = (json.dumps(receipt, indent=2) + '\n').encode()
    payload['SHA256SUMS'] = ''.join(f'{sha(data)}  {name}\n' for name, data in sorted(payload.items())).encode('ascii')
    output = args.output_directory.resolve()
    # Never merge into an old SD folder which might contain arming markers.
    output.mkdir(parents=True, exist_ok=False)
    for name, data in sorted(payload.items()):
        dst = output / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(data)
        assert sha(dst.read_bytes()) == sha(data)
    archive = output.with_suffix('.zip')
    if archive.exists():
        raise FileExistsError(archive)
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for name, data in sorted(payload.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 14, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (0o100755 if name.endswith('.sh') else 0o100644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        final = {name: z.read(name) for name in z.namelist()}
        assert final == payload
        validate_payload(final, release, stock, port)
        for row in final['SHA256SUMS'].decode().splitlines():
            expected, name = row.split('  ', 1)
            assert sha(final[name]) == expected
    validation = dict(**receipt, sd_files=len(payload), zip_sha256=sha(archive.read_bytes()),
                      zip_bytes=archive.stat().st_size, zip=str(archive), directory=str(output))
    output.with_suffix('.validation.json').write_text(json.dumps(validation, indent=2) + '\n')
    print(json.dumps(validation, indent=2))


if __name__ == '__main__':
    main()
