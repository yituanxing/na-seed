#!/usr/bin/env python3
"""Original signed three-ELF/ISA physical ext4 and deleted transient test keys."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import subprocess
import sys
import tempfile

source = Path(__file__).with_name('m206-alpine-disk-check.py')
spec = spec_from_file_location('m222_m206_disk', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M222 requires the established signed physical-ext4 owner')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)


def verify(port: str, root: Path, disk: Path) -> None:
    home = Path(__file__).resolve().parent.parent
    owner.verify(port, root, disk,
                 candidates=home / 'scripts/m222-candidate-elfs.tsv',
                 packages=home / 'rootfs/alpine/packages-m222-candidates.txt',
                 expected_count=3, label='M222')
    m191 = owner.load('m197-alpine-disk-check.py', 'm222_m197').load(
        'm195-alpine-disk-check.py', 'm222_m195').load_m191()
    with tempfile.TemporaryDirectory(prefix='m222-private-key-check-') as temp:
        partition = Path(temp) / 'ext4.img'
        m191.extract_partition(disk.resolve(strict=True), partition)
        for phase in ('first', 'reopen'):
            for leaf in ('id_ed25519', 'id_ed25519.pub'):
                guest = f'/root/m222-auth/{phase}/{leaf}'
                process = subprocess.run(['debugfs', '-R', f'stat {guest}', str(partition)],
                                         capture_output=True, text=True, check=False)
                if process.returncode == 0 and 'Inode:' in process.stdout:
                    raise ValueError('[M222 SECRET FAIL] guest test key survived: ' + guest)
    print('[M222 TEST KEY CLEANUP PASS] no first/reopen client private/public key on physical ext4')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('usage: m222-alpine-disk-check.py PORT SIGNED_ROOT DISK')
    verify(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]))
