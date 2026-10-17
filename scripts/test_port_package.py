"""Check real Java artifact family and reject incomplete/mixed vehicle builds.
No synthetic native executable is shipped or run by these tests.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'modkit'))
import port_build


def reject(directory, message):
    try:
        port_build.validate_artifacts(directory)
    except (RuntimeError, ValueError) as exc:
        assert message in str(exc), str(exc)
    else:
        raise AssertionError('Unexpectedly accepted mixed/incomplete artifact set')


def main():
    artifacts = ROOT / 'build-local/port-artifacts'
    receipt = json.loads((artifacts / 'java-build.json').read_text())
    assert receipt['source_fingerprint'] == port_build.fingerprint()
    with zipfile.ZipFile(artifacts / port_build.JAR) as z:
        classes = [n for n in z.namelist() if n.endswith('.class')]
        assert len(classes) == receipt['class_count'] == 26
        assert all(struct.unpack('>H', z.read(n)[6:8])[0] == 48 for n in classes)
        assert len([n for n in classes if n.startswith(port_build.AA + 'AndroidAuto2Subsystem')]) == 16
        assert not any('carplay/' in n or 'ClusterAAMirror' in n or 'Activator' in n for n in classes)
        assert z.read('cluster_config.json') == (artifacts / 'cluster_config.json').read_bytes()
    case_root = ROOT / 'build-local/port-package-tests'
    case_root.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=case_root))
    copy = work / 'artifacts'
    copy.mkdir()
    for name in (port_build.JAR, 'cluster_config.json', 'java-build.json'):
        shutil.copyfile(artifacts / name, copy / name)
    reject(copy, 'Missing native-build.json')
    sd = work / 'must-not-exist-sd'
    command = [sys.executable, ROOT / 'modkit/build_audi_sd.py', '--stock-gal',
               ROOT.parent.parent / 'research/evidence/mh2p_2026_10_11_14_08/files/mnt/app/eso/bin/apps/gal',
               '--port-artifacts', copy, '--output-directory', sd]
    outcome = subprocess.run([str(p) for p in command], capture_output=True, text=True)
    assert outcome.returncode != 0 and 'Missing native-build.json' in outcome.stderr
    assert not sd.exists() and not sd.with_suffix('.zip').exists()
    with zipfile.ZipFile(ROOT / 'builds/ClusterIntegration_v0034_beta2_candidate_90d0b76_audi.zip') as release:
        for name in ('cluster', 'gal_cluster.so'):
            data = release.read('Mods/ClusterIntegration/Update/' + name)
            (copy / name).write_bytes(data)
    native = dict(schema=1, kind='local-port-qnx-native', source_fingerprint=port_build.fingerprint(),
                  files={n: port_build.sha((copy / n).read_bytes()) for n in ('cluster', 'gal_cluster.so')})
    (copy / 'native-build.json').write_text(json.dumps(native))
    reject(copy, 'was not rebuilt from current local port sources')
    original = (copy / port_build.JAR).read_bytes()
    (copy / port_build.JAR).write_bytes(original + b'altered')
    reject(copy, 'Changed build artifact')
    (copy / port_build.JAR).write_bytes(original)
    native['source_fingerprint'] = 'stale-source'
    (copy / 'native-build.json').write_text(json.dumps(native))
    reject(copy, 'Stale or wrong build receipt')
    native['source_fingerprint'] = port_build.fingerprint()
    native['files'].pop('cluster')
    (copy / 'native-build.json').write_text(json.dumps(native))
    reject(copy, 'Wrong artifact file set')
    try:
        port_build.arm_elf(b'MZ Windows executable', 'cluster')
    except ValueError as exc:
        assert 'not ARM32' in str(exc)
    else:
        raise AssertionError('Accepted Windows binary')
    print(f'PASS: complete Java family/config; incomplete, upstream, changed, stale, missing and non-ARM native artifacts rejected; {work}')


if __name__ == '__main__':
    main()
