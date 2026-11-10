#!/usr/bin/env python3
"""M225 exact native signed APK SHA + two-boot actual ext4 FTP witnesses."""
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


owner = load('m206-alpine-disk-check.py', 'm225_signed_m206')
host = load('m225-host-proof.py', 'm225_ftp_witness')
ROOT = Path(__file__).resolve().parent.parent
BINARIES = ('/usr/bin/lftp', '/usr/bin/rclone')


def stage(isa: str, signed_root: Path) -> None:
    m191 = owner.load('m191-alpine-root-disk.py', 'm225_stage_m191')
    if isa not in m191.ARCH_MACHINE:
        raise ValueError('[M225 STAGE FAIL] unsupported native ISA')
    signed_root = signed_root.resolve(strict=True)
    for path in BINARIES:
        raw = m191.guest_path(signed_root, str(Path(path).parent)) / Path(path).name
        if raw.exists() or raw.is_symlink():
            raise ValueError('[M225 STAGE FAIL] original APK ELF already preseeded: ' + path)


def signed_exact_sha(isa: str, partition: Path, m191) -> None:
    inventory = ROOT / f'build/external/m225-candidate-inventory/{isa}/candidates.tsv'
    if inventory.is_symlink():
        raise ValueError('[M225 SIGNED FAIL] symlinked signed source inventory')
    with inventory.open(newline='', encoding='utf-8') as stream:
        rows = list(csv.reader(stream, delimiter='\t'))
    if not rows or rows[0] != ['package', 'elf_path', 'status', 'sha256']:
        raise ValueError('[M225 SIGNED FAIL] missing exact signed APK SHA table')
    expected = {('lftp', 'usr/bin/lftp'), ('rclone', 'usr/bin/rclone')}
    seen = set()
    shas = set()
    for row in rows[1:]:
        if (len(row) != 4 or (row[0], row[1]) not in expected or
                (row[0], row[1]) in seen or row[2] != 'NATIVE_ELF' or
                not re.fullmatch('[0-9a-f]{64}', row[3])):
            raise ValueError('[M225 SIGNED FAIL] malformed/foreign original ELF census')
        if hashlib.sha256(m191.debugfs_cat(partition, '/' + row[1])).hexdigest() != row[3]:
            raise ValueError('[M225 SIGNED FAIL] physical ext4 ELF SHA differs from signed APK')
        seen.add((row[0], row[1]))
        shas.add(row[3])
    if seen != expected or len(shas) != 2:
        raise ValueError('[M225 SIGNED FAIL] missing/duplicate original APK native SHA')


def verify(isa: str, signed_root: Path, disk: Path, first: Path, reopen: Path):
    stage(isa, signed_root)
    host.two_phase_sources(first, reopen)
    owner.verify(isa, signed_root, disk,
                 candidates=ROOT / 'scripts/m225-candidate-elfs.tsv',
                 packages=ROOT / 'rootfs/alpine/packages-m225-candidates.txt',
                 expected_count=2, label='M225')
    m191 = owner.load('m191-alpine-root-disk.py', 'm225_final_m191')
    with tempfile.TemporaryDirectory(prefix='m225-signed-physical-') as temp:
        partition = Path(temp) / 'root.ext4'
        m191.extract_partition(disk.resolve(strict=True), partition)
        signed_exact_sha(isa, partition, m191)
        for phase, directory in (('first', first), ('reopen', reopen)):
            host.physical_files(phase, directory,
                                lambda path: m191.debugfs_cat(partition, path),
                                ftp_only=True)
    print('[M225 SIGNED FTP TWO-BOOT DISK PASS] exact official APK SHA x2, '
          'real first/reopen FTP downloads+uploads on physical ext4, M197 fsck')


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == 'stage':
        stage(sys.argv[2], Path(sys.argv[3]))
    elif len(sys.argv) == 7 and sys.argv[1] == 'verify':
        verify(sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
               Path(sys.argv[5]), Path(sys.argv[6]))
    else:
        raise SystemExit('usage: m225-alpine-disk-check.py stage ISA ROOT | '
                         'verify ISA ROOT DISK HOST_FIRST HOST_REOPEN')
