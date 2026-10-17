"""Run production recovery code on isolated fixtures with Bash OS adapters.
This is not QNX validation. No live MMI, mount, process kill or install.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STOCK_SHA = '9d455b764854f7977971ae977b21fe7560d8a40cca86d31cb9c9a7f5821c0ecf'


def bash_path(path):
    path = Path(path).resolve()
    return '/' + path.drive[0].lower() + path.as_posix()[2:] if path.drive else str(path)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--bash', type=Path, default=Path('C:/Program Files/Git/bin/bash.exe'))
    p.add_argument('--stock-gal', type=Path, required=True)
    args = p.parse_args()
    data = args.stock_gal.read_bytes()
    assert hashlib.sha256(data).hexdigest() == STOCK_SHA
    for script in [* (ROOT / 'modkit').rglob('*.sh'), ROOT / 'tests/recovery_shell_harness.sh']:
        subprocess.run([str(args.bash), '-O', 'extglob', '-n', bash_path(script)], check=True)
    test_root = ROOT / 'build-local/recovery-tests'
    test_root.mkdir(parents=True, exist_ok=True)
    base = Path(tempfile.mkdtemp(prefix='run-', dir=test_root))
    results = []
    for fault, expected in [('', 0), ('backup', 4), ('stage', 4), ('corrupt_stage', 4),
                            ('rename', 5), ('helper_backup', 6), ('jar_remove', 6),
                            ('no_compare', 3), ('bad_stock', 3), ('missing_reference', 3),
                            ('bad_unit_original', 0)]:
        card = base / (fault or 'success')
        upd = card / 'Mods/ClusterIntegration/Update'
        stock = upd / 'Stock'
        stock.mkdir(parents=True)
        for name in ('uninstall.sh', 'logging.sh'):
            shutil.copyfile(ROOT / 'modkit/Mods/ClusterIntegration/Update' / name, upd / name)
        (stock / 'gal.real').write_bytes(data)
        (stock / 'gal.reference').write_bytes(data)
        if fault == 'bad_stock':
            (stock / 'gal.real').write_bytes(b'\x7fELFbad saved stock')
        if fault == 'missing_reference':
            (stock / 'gal.reference').unlink()
        env = os.environ.copy()
        env['RECOVERY_FAULT'] = fault
        cmd = [str(args.bash), bash_path(ROOT / 'tests/recovery_shell_harness.sh'), bash_path(upd), '--self-test']
        completed = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=30)
        logs = list((card / 'Logs/ClusterIntegration').glob('rollback-*/commands.log'))
        assert len(logs) == 1, completed.stdout + completed.stderr
        log = logs[0].read_text()
        assert completed.returncode == expected, (fault, completed.returncode, log[-5000:])
        assert f'EXIT_STATUS={expected}' in log
        assert 'FORBIDDEN_HOST_' not in log
        assert 'formatted date unsupported' not in log
        assert not (card / 'Mods/ClusterIntegration/uninstall.txt').exists()
        run = logs[0].parent
        active = run / 'sandbox-root/apps/gal'
        helper = run / 'sandbox-root/apps/cluster/cluster'
        jar = run / 'sandbox-root/jars/ClusterIntegration_selftest.jar'
        if expected == 0:
            assert active.read_bytes() == data
            assert (run / 'sandbox-root/apps/gal.real').read_bytes() == data
            assert not helper.exists() and not jar.exists()
            assert (jar.parent / 'NaviCompass.jar').read_bytes() == b'preserve NavCompass\n'
            assert (jar.parent / 'AppDevelopment.jar').read_bytes() == b'preserve stock jar\n'
            for name in ('gal_cluster.so', 'dio_cluster.so', 'cluster_config.json', 'diag_mode', 'gal_env'):
                assert not (helper.parent / name).exists()
            assert not (jar.parent / 'ClusterIntegration_StorageMountHandler_stub.jar').exists()
            assert 'SELFTEST_OK' in (run / 'result.txt').read_text()
            # Every pre-change backup retained under the isolated run.
            backups = list((run / 'sandbox-backups').glob('*.removed.*'))
            assert len(backups) == (10 if fault == 'bad_unit_original' else 9)
        elif fault in ('backup', 'stage', 'corrupt_stage', 'rename'):
            assert active.read_bytes() == b'#!/bin/ksh\n'
            assert helper.exists() and jar.exists()
        elif fault == 'helper_backup':
            assert active.read_bytes() == data and helper.exists()
        elif fault == 'jar_remove':
            assert active.read_bytes() == data and jar.exists()
        results.append(dict(case=fault or 'success', expected_exit=expected, passed=True))
    # Concurrent invocation gets distinct log directories, without dates.
    success_upd = base / 'success/Mods/ClusterIntegration/Update'
    peers = [subprocess.Popen([str(args.bash), bash_path(ROOT / 'tests/recovery_shell_harness.sh'),
                              bash_path(success_upd), '--self-test'], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE) for _ in range(2)]
    for peer in peers:
        out, err = peer.communicate(timeout=30)
        assert peer.returncode == 0, (out, err)
    assert len(list((base / 'success/Logs/ClusterIntegration').glob('rollback-*/result.txt'))) == 3
    import json
    (base / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(f'PASS: shell syntax, {len(results)} recovery/failure cases, concurrent run allocation; {base}')


if __name__ == '__main__':
    main()
