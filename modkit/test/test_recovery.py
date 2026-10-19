"""Portable host tests using synthetic data only; no QNX SDK, firmware or car."""
import argparse, json, os, shutil, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
FILES = ('uninstall.sh', 'logging.sh', 'comparison.sh', 'elf-magic.bin', 'equal-a.bin', 'equal-b.bin', 'selftest-original.bin')
CASES = [('', 0), ('backup', 4), ('corrupt_backup', 4), ('stage', 4), ('stage_dio', 4), ('corrupt_stage', 4), ('chmod_stage', 4), ('rename', 5), ('helper_backup', 6), ('jar_remove', 6), ('sync', 6), ('bad_original', 3), ('short_original', 3), ('header_read', 3), ('no_compare', 3), ('missing_magic', 3), ('wrong_magic', 3), ('bundle_good', 0), ('bundle_always_equal', 3), ('bundle_prefix', 3), ('bundle_io', 3), ('hash_only', 0)]

def bash_path(p):
    p = Path(p).resolve()
    s = p.as_posix()
    return '/' + s[0].lower() + s[2:] if p.drive else s

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bash', default=os.environ.get('BASH_BIN', 'C:/Program Files/Git/bin/bash.exe' if os.name == 'nt' else 'bash'))
    p.add_argument('--output', type=Path)
    a = p.parse_args()
    out = a.output or ROOT / 'build-local/recovery-tests'
    out.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(dir=out, prefix='run-'))
    scripts = [ROOT / 'modkit/Mods/ClusterIntegration/Update' / n for n in FILES if n.endswith('.sh')] + [ROOT / 'modkit/test/recovery_harness.sh']
    for f in scripts:
        subprocess.run([a.bash, '-O', 'extglob', '-n', bash_path(f)], check=True)

    def setup(name):
        card = work / name
        upd = card / 'Mods/ClusterIntegration/Update'
        upd.mkdir(parents=True)
        for n in FILES:
            shutil.copyfile(ROOT / 'modkit/Mods/ClusterIntegration/Update' / n, upd / n)
        return (card, upd)

    def command(upd, *args):
        return [a.bash, bash_path(ROOT / 'modkit/test/recovery_harness.sh'), bash_path(upd), *args]
    results = []
    for fault, expected in CASES:
        card, upd = setup(fault or 'success with spaces')
        if fault == 'missing_magic':
            (upd / 'elf-magic.bin').unlink()
        if fault == 'wrong_magic':
            (upd / 'elf-magic.bin').write_bytes(b'BAD!')
        if fault.startswith('bundle_'):
            (upd / 'qnx_file_equal').write_text('#!/usr/bin/env bash\ncase ${RECOVERY_FAULT:-} in\n bundle_always_equal) exit 0 ;;\n bundle_prefix) [[ $2 != */short ]] || exit 0 ;;\nesac\ncommand cmp -s "$1" "$2"\nrc=$?\n[[ ${RECOVERY_FAULT:-} != bundle_io || $rc != 2 ]] || rc=1\nexit "$rc"\n', newline='\n')
            (upd / 'qnx_file_equal').chmod(0o755)
        env = dict(os.environ, RECOVERY_FAULT=fault)
        r = subprocess.run(command(upd, '--self-test'), env=env, capture_output=True, text=True, timeout=60)
        assert r.returncode == expected, (fault, r.returncode, r.stdout, r.stderr)
        logs = list((card / 'Logs/ClusterIntegration').glob('rollback-*'))
        assert len(logs) == 1
        log = (logs[0] / 'commands.log').read_text(errors='replace')
        result = (logs[0] / 'result.txt').read_text()
        assert 'FORBIDDEN_HOST_' not in log and f'EXIT_STATUS={expected}' in result, (fault, log)
        apps = logs[0] / 'sandbox-root/apps'
        jars = logs[0] / 'sandbox-root/jars'
        if expected == 0:
            assert 'RESULT=SELFTEST_OK' in result
            assert (apps / 'gal').read_bytes() == (apps / 'gal.real').read_bytes()
            assert (apps / 'dio_manager').read_bytes() == (apps / 'dio_manager.real').read_bytes()
            assert (jars / 'NaviCompass.jar').exists() and (jars / 'AppDevelopment.jar').exists()
            assert not (jars / 'ClusterIntegration_StorageMountHandler_stub.jar').exists()
        if expected == 4:
            assert (apps / 'gal').read_text().startswith('#!/bin/ksh') and (apps / 'dio_manager').read_text().startswith('#!/bin/ksh')
            assert (jars / 'ClusterIntegration_selftest.jar').exists()
        if fault == 'helper_backup':
            assert (apps / 'cluster/cluster').exists()
        if fault == 'jar_remove':
            assert (jars / 'ClusterIntegration_selftest.jar').exists()
        results.append(dict(case=fault or 'success', exit=r.returncode))
        print('PASS', fault or 'success')
    # Read-only check must never mount, signal or create uninstall markers.
    card, upd = setup('check')
    r = subprocess.run(command(upd, '--check'), capture_output=True, text=True, timeout=30)
    assert r.returncode == 0
    log = next((card / 'Logs/ClusterIntegration').glob('rollback-*/commands.log')).read_text(errors='replace')
    assert 'RESULT=CHECK_OK' in log and 'FORBIDDEN_HOST_' not in log
    assert not (upd.parent / 'uninstall.txt').exists()
    # Clock stays at 1970; mkdir must allocate unique concurrent log IDs.
    card, upd = setup('concurrent')
    children = [subprocess.Popen(command(upd, '--check'), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) for _ in range(8)]
    for proc in children:
        stdout, stderr = proc.communicate(timeout=60)
        assert proc.returncode == 0, (stdout, stderr)
    assert len(list((card / 'Logs/ClusterIntegration').glob('rollback-*/result.txt'))) == 8
    # Invalid requests cannot accidentally start a live rollback.
    for arg in ('--unknown', '--check extra'):
        card, upd = setup('invalid-' + arg.replace(' ', '-').replace('--', ''))
        r = subprocess.run(command(upd, *arg.split()), capture_output=True, text=True, timeout=30)
        assert r.returncode == 2
    summary = dict(fault_cases=results, check=True, concurrent_runs=8, invalid_arguments=2, synthetic_only=True, work=str(work))
    (work / 'RESULTS.json').write_text(json.dumps(summary, indent=2) + '\n')
    print('PASS recovery suite:', len(CASES), 'fixture cases + check +8 concurrent logs +2 invalid requests;', work)
if __name__ == '__main__':
    main()
