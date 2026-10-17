"""Build/validate matched local Java + ARM/QNX lifecycle artifacts.
No upstream native binary is accepted as a substitute for a local build.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile
import tarfile

ROOT = Path(__file__).resolve().parents[1]
AA = 'de/audi/app/terminalmode/smartphone/androidauto2/'
JAVA_FILES = ['AndroidAuto2Subsystem.java', 'AndroidAuto2EventListener.java',
              'nav/AndroidAutoClusterIntegration.java', 'nav/ClusterLifecycle.java',
              'nav/ClusterLifecycleFiles.java', 'nav/ClusterMapController.java']
API_SHA = 'f6f43b1b8af5ef0ef10e365ce7288beebb638d5f504610cef15c1374f3ee15d4'
ECJ_SHA512 = 'ab441acf5551a7dc81c353eaccb3b3df9e89a48987294d19e39acdb83a5b640fcdff7414cee29f5b96eaa8826647f1d5323e185018fe33a64c402d69c73c9158'
JAR = 'ClusterIntegration_mib2p_lifecycle_aa.jar'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sources():
    files = [p for p in (ROOT / 'src').iterdir() if p.suffix in ('.h', '.c')]
    files += [ROOT / 'lsd' / AA / name for name in JAVA_FILES]
    files += [ROOT / 'lsd/cluster_config.json']
    return {p.relative_to(ROOT).as_posix(): sha(p.read_bytes()) for p in sorted(files)}


def fingerprint():
    return sha(json.dumps(sources(), sort_keys=True).encode())


def run(command):
    print('BUILD:', ' '.join(str(p) for p in command), flush=True)
    subprocess.run([str(p) for p in command], check=True)


def build_java(args):
    assert sha(args.stock_api.read_bytes()) == API_SHA, 'Stock API hash differs'
    assert hashlib.sha512(args.ecj.read_bytes()).hexdigest() == ECJ_SHA512, 'Compiler hash differs'
    work = Path(tempfile.mkdtemp(prefix='java-', dir=args.output))
    classes = work / 'classes'
    stub = ROOT / 'modkit/Mods/ClusterIntegration/Update/audi/ClusterIntegration_StorageMountHandler_stub.jar'
    java = args.java_home / 'bin' / ('java.exe' if os.name == 'nt' else 'java')
    run([java, '-jar', args.ecj, '-source', '1.4', '-target', '1.4', '-encoding', 'UTF-8', '-nowarn',
         '-bootclasspath', args.stock_api, '-classpath', stub, '-d', classes,
         *[ROOT / 'lsd' / AA / name for name in JAVA_FILES]])
    generated = {p.relative_to(classes).as_posix(): p.read_bytes() for p in classes.rglob('*.class')}
    assert generated and all(struct.unpack('>H', data[6:8])[0] == 48 for data in generated.values())
    # Fresh output + complete rebuilt outer/inner families. No upstream
    # Subsystem/Integration inner classes or unrelated placeholders are mixed in.
    assert len([n for n in generated if n.startswith(AA + 'AndroidAuto2Subsystem')]) == 16
    assert AA + 'nav/ClusterLifecycle.class' in generated
    assert AA + 'nav/ClusterLifecycleFiles.class' in generated
    assert AA + 'AndroidAuto2EventListener.class' in generated
    assert not any('carplay/' in n or 'Activator' in n or 'ClusterAAMirror' in n for n in generated)
    config = json.loads((ROOT / 'lsd/cluster_config.json').read_bytes())
    config['buildVersion'] = 'mib2p-lifecycle-' + fingerprint()[:12]
    config['config']['enableLifecyclePort'] = True
    config['config']['enableMapRender'] = False  # First diagnostic pass.
    config_data = (json.dumps(config, indent=2) + '\n').encode()
    manifest = ('Manifest-Version: 1.0\r\nImplementation-Title: ClusterIntegration\r\n'
                'Implementation-Version: mib2p-lifecycle-' + fingerprint()[:12] + '\r\n\r\n').encode()
    with zipfile.ZipFile(args.output / JAR, 'w', zipfile.ZIP_DEFLATED) as jar:
        jar.writestr('META-INF/MANIFEST.MF', manifest)
        for name, data in sorted(generated.items()):
            jar.writestr(name, data)
        # Native/Java use the source config schema, not the newer release schema.
        jar.writestr('cluster_config.json', config_data)
    (args.output / 'cluster_config.json').write_bytes(config_data)
    receipt = dict(schema=1, kind='local-port-java', source_fingerprint=fingerprint(),
                   files={n: sha((args.output / n).read_bytes()) for n in (JAR, 'cluster_config.json')},
                   class_count=len(generated), class_major=48, stock_api_sha256=API_SHA,
                   full_subsystem_family=True, qnx_j9_tested=False)
    (args.output / 'java-build.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))


def arm_elf(data, name):
    if len(data) < 52 or data[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', data, 18)[0] != 40:
        raise ValueError(f'{name} is not ARM32 little-endian ELF')
    kind = struct.unpack_from('<H', data, 16)[0]
    if kind not in ((3,) if name.endswith('.so') else (2, 3)):
        raise ValueError(f'{name} is not a linked executable/shared object')
    if ('MIB2P_PORT_V1:' + fingerprint()).encode() not in data:
        raise ValueError(f'{name} was not rebuilt from current local port sources')


def build_native(args):
    qcc = shutil.which('qcc')
    target = os.environ.get('QNX_TARGET', '')
    if not qcc or not target:
        raise RuntimeError('QNX SDP 6.6 is not configured. Install the Linux SDK and source qnx660-env.sh; upstream binaries cannot implement this port.')
    if not (Path(target).parent.parent / 'qnx660-env.sh').is_file():
        raise RuntimeError('Expected the QNX SDP 6.6 environment, not a newer default SDK')
    work = Path(tempfile.mkdtemp(prefix='native-', dir=args.output))
    flags = ['-Vgcc_ntoarmv7le', '-O2', '-g', '-Wall', '-std=gnu99',
             '-DMIB2P_BUILD_FINGERPRINT="' + fingerprint() + '"']
    run([qcc, *flags, '-shared', '-fPIC', '-o', work / 'gal_cluster.so',
         ROOT / 'src/gal_cluster.c', '-lscreen', '-lc'])
    # GL functions are resolved via eglGetProcAddress; deliberately no GLESv2
    # stub link. NvMedia/parser APIs are loaded from the MMI at runtime.
    run([qcc, *flags, '-o', work / 'cluster', ROOT / 'src/cluster.c', '-lscreen', '-lEGL', '-lz', '-lc'])
    for name in ('gal_cluster.so', 'cluster'):
        arm_elf((work / name).read_bytes(), name)
    for name in ('gal_cluster.so', 'cluster'):
        shutil.copyfile(work / name, args.output / name)
    receipt = dict(schema=1, kind='local-port-qnx-native', source_fingerprint=fingerprint(),
                   compiler=qcc, qnx_target=target, variant='gcc_ntoarmv7le',
                   files={n: sha((args.output / n).read_bytes()) for n in ('gal_cluster.so', 'cluster')},
                   target_executed=False)
    (args.output / 'native-build.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))


def validate_artifacts(directory):
    result = {}
    for receipt_name, expected_kind in [('java-build.json', 'local-port-java'), ('native-build.json', 'local-port-qnx-native')]:
        path = directory / receipt_name
        if not path.is_file():
            raise RuntimeError(f'Missing {receipt_name}; cannot package disconnect/reconnect with upstream native binaries')
        receipt = json.loads(path.read_text())
        if receipt['kind'] != expected_kind or receipt['source_fingerprint'] != fingerprint():
            raise ValueError(f'Stale or wrong build receipt: {path}')
        expected_names = {JAR, 'cluster_config.json'} if expected_kind == 'local-port-java' else {'gal_cluster.so', 'cluster'}
        if set(receipt['files']) != expected_names:
            raise ValueError(f'Wrong artifact file set: {path}')
        for name, expected in receipt['files'].items():
            data = (directory / name).read_bytes()
            if sha(data) != expected:
                raise ValueError(f'Changed build artifact: {name}')
            result[name] = data
    for name in ('cluster', 'gal_cluster.so'):
        arm_elf(result[name], name)
    config = json.loads(result['cluster_config.json'])
    assert config['config']['enableLifecyclePort'] is True
    assert config['config']['enableMapRender'] is False
    result['java-build.json'] = (directory / 'java-build.json').read_bytes()
    result['native-build.json'] = (directory / 'native-build.json').read_bytes()
    return result


def stage_vm(args):
    stage = Path(tempfile.mkdtemp(prefix='ubuntu1204-source-', dir=args.output))
    entries = sources()
    for name in entries:
        destination = stage / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, destination)
    for name in ('native_vm_build.sh', 'native_vm_receipt.py'):
        (stage / name).write_bytes((ROOT / 'modkit' / name).read_bytes().replace(b'\r\n', b'\n'))
    shutil.copyfile(ROOT / 'LICENSE.md', stage / 'LICENSE.md')
    (stage / 'source-manifest.json').write_text(json.dumps(dict(source_fingerprint=fingerprint(), files=entries), indent=2) + '\n')
    run([os.sys.executable, stage / 'native_vm_receipt.py', 'check'])
    with tarfile.open(stage.with_suffix('.tar.gz'), 'w:gz') as archive:
        archive.add(stage, arcname='audi-qnx-source')
    print(f'Ubuntu 12.04 source bundle: {stage.with_suffix(".tar.gz")}')


def import_vm(args):
    if not args.native_directory:
        raise RuntimeError('Specify --native-directory containing the two VM outputs and native-build.json')
    incoming = args.native_directory
    receipt = json.loads((incoming / 'native-build.json').read_text())
    if receipt['kind'] != 'local-port-qnx-native' or receipt['source_fingerprint'] != fingerprint():
        raise ValueError('VM build uses stale/wrong source fingerprint')
    if set(receipt['files']) != {'gal_cluster.so', 'cluster'}:
        raise ValueError('VM build has wrong file set')
    for name, expected in receipt['files'].items():
        data = (incoming / name).read_bytes()
        if sha(data) != expected:
            raise ValueError(f'VM output changed: {name}')
        arm_elf(data, name)
    # Validate every incoming output before replacing any desktop artifact.
    for name in ('gal_cluster.so', 'cluster', 'native-build.json'):
        shutil.copyfile(incoming / name, args.output / name)
    validate_artifacts(args.output)
    print('Imported and validated matched VM native + desktop Java artifacts')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('part', choices=['java', 'native', 'validate', 'stage-vm', 'import-vm'])
    p.add_argument('--output', type=Path, default=ROOT / 'build-local/port-artifacts')
    p.add_argument('--stock-api', type=Path, default=ROOT.parent / 'mib2p-android-auto-cluster/android_auto/mh2p/build-java-routing-transport/stock-api.jar')
    p.add_argument('--ecj', type=Path, default=ROOT.parent / 'mib2p-android-auto-cluster/android_auto/mh2p/build-java-tools/ecj-4.20.jar')
    p.add_argument('--java-home', type=Path, default=Path('C:/Program Files/Java/jdk-23'))
    p.add_argument('--native-directory', type=Path)
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.part == 'java': build_java(args)
    elif args.part == 'native': build_native(args)
    elif args.part == 'stage-vm': stage_vm(args)
    elif args.part == 'import-vm': import_vm(args)
    else: print(f'Validated {len(validate_artifacts(args.output))} matched artifact files')


if __name__ == '__main__':
    main()
