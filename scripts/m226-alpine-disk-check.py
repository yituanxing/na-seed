#!/usr/bin/env python3
"""M226 physical ext4 + exact official signed APK SHA, restored remote bytes."""
import csv
import hashlib
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import re
import sys
import tempfile


def load(name, alias):
    spec = spec_from_file_location(alias, Path(__file__).with_name(name))
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


owner = load('m206-alpine-disk-check.py', 'm226_signed_m206')
host = load('m226-host-proof.py', 'm226_real_rest_server_proof')
ROOT = Path(__file__).resolve().parent.parent
BINARY = '/usr/bin/restic'


def stage(isa, root):
    m191 = owner.load('m191-alpine-root-disk.py', 'm226_stage_m191')
    if isa not in m191.ARCH_MACHINE:
        raise ValueError('[M226 STAGE FAIL] invalid native architecture')
    root = root.resolve(strict=True)
    path = m191.guest_path(root, '/usr/bin') / 'restic'
    if path.exists() or path.is_symlink():
        raise ValueError('[M226 STAGE FAIL] original signed restic ELF preseeded')


def signed_exact_sha(isa, partition, m191):
    table = ROOT / f'build/external/m226-candidate-inventory/{isa}/candidates.tsv'
    if table.is_symlink():
        raise ValueError('[M226 SIGNED FAIL] inventory symlink')
    with table.open(newline='', encoding='utf-8') as stream:
        rows = list(csv.reader(stream, delimiter='\t'))
    if (len(rows) != 2 or rows[0] != ['package', 'elf_path', 'status', 'sha256'] or
            rows[1][0:3] != ['restic', 'usr/bin/restic'] + ['NATIVE_ELF'] or
            len(rows[1]) != 4 or not re.fullmatch(r'[0-9a-f]{64}', rows[1][3])):
        raise ValueError('[M226 SIGNED FAIL] no exact signed restic inventory identity')
    actual = m191.debugfs_cat(partition, BINARY)
    if hashlib.sha256(actual).hexdigest() != rows[1][3]:
        raise ValueError('[M226 SIGNED FAIL] guest physical ELF not byte-identical with official APK')


def verify(isa, root, disk, first, reopen):
    stage(isa, root)
    host.two_phase_sources(first, reopen)
    owner.verify(isa, root, disk,
                 candidates=ROOT / 'scripts/m226-candidate-elfs.tsv',
                 packages=ROOT / 'rootfs/alpine/packages-m226-candidates.txt',
                 expected_count=1, label='M226')
    m191 = owner.load('m191-alpine-root-disk.py', 'm226_signed_m191')
    with tempfile.TemporaryDirectory(prefix='m226-signed-physical-') as temp:
        partition = Path(temp) / 'root.ext4'
        m191.extract_partition(disk.resolve(strict=True), partition)
        signed_exact_sha(isa, partition, m191)
        read = lambda path: m191.debugfs_cat(partition, path)
        for phase, origin in (('first', first), ('reopen', reopen)):
            host.physical_files(phase, origin, read)
    print('[M226 SIGNED RESTIC TWO-BOOT DISK PASS] original Alpine restic SHA, '
          'remote first/reopen source and both native restored files verified '
          'on physical ext4; M197 fsck/public software retained')


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == 'stage':
        stage(sys.argv[2], Path(sys.argv[3]))
    elif len(sys.argv) == 7 and sys.argv[1] == 'verify':
        verify(sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
               Path(sys.argv[5]), Path(sys.argv[6]))
    else:
        raise SystemExit('usage: m226-alpine-disk-check.py stage ISA ROOT | '
                         'verify ISA ROOT DISK HOST_FIRST HOST_REOPEN')
