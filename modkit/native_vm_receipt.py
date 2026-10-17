"""Python 2.7/3 compatible native source/output verification for Ubuntu 12.04.
This file runs in the exported bundle; desktop assembly uses port_build.py.
"""
from __future__ import print_function
import hashlib
import json
import os
import struct
import sys


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        while True:
            block = f.read(1024 * 1024)
            if not block:
                break
            h.update(block)
    return h.hexdigest()


def main():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    with open('source-manifest.json') as f:
        manifest = json.load(f)
    files = manifest['files']
    expected = hashlib.sha256(json.dumps(files, sort_keys=True).encode('ascii')).hexdigest()
    if expected != manifest['source_fingerprint']:
        raise ValueError('Source manifest fingerprint differs')
    for name, digest in files.items():
        if name.startswith('/') or '..' in name.split('/') or sha(name) != digest:
            raise ValueError('Source changed: ' + name)
    if len(sys.argv) != 2 or sys.argv[1] not in ('check', 'receipt'):
        raise ValueError('usage: python native_vm_receipt.py check|receipt')
    if sys.argv[1] == 'check':
        print('Verified source fingerprint: ' + expected)
        return
    marker = ('MIB2P_PORT_V1:' + expected).encode('ascii')
    for name in ('cluster', 'gal_cluster.so'):
        with open('out/' + name, 'rb') as f:
            data = f.read()
        if len(data) < 52 or data[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', data, 18)[0] != 40:
            raise ValueError('Not ARM32 ELF: ' + name)
        kind = struct.unpack_from('<H', data, 16)[0]
        if kind not in ((3,) if name.endswith('.so') else (2, 3)) or marker not in data:
            raise ValueError('Wrong executable type/build fingerprint: ' + name)
    receipt = dict(schema=1, kind='local-port-qnx-native', source_fingerprint=expected,
                   compiler='qcc (Ubuntu 12.04 VM)', qnx_target=os.environ.get('QNX_TARGET'),
                   variant='gcc_ntoarmv7le', target_executed=False,
                   files=dict((n, sha('out/' + n)) for n in ('cluster', 'gal_cluster.so')))
    with open('out/native-build.json', 'w') as f:
        json.dump(receipt, f, indent=2)
        f.write('\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
