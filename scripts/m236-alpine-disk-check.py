#!/usr/bin/env python3
"""Independent physical ext4 M236 signed-SHA and seven real semantics oracle.

M197 owns the original official-Alpine first/reopen QEMU and filesystem proof;
M206 owns ISA-qualified ELF/world/installed evidence. M236 additionally pins
every ext4 binary to its independently downloaded, signed-APK census SHA and
reads actual first and reopened functional bytes from the same partition.
"""
from __future__ import annotations

import csv
import hashlib
from importlib.util import module_from_spec, spec_from_file_location
import json
import re
from pathlib import Path
import struct
import zlib
import sys
import tempfile
import xml.etree.ElementTree as ET


HOME = Path(__file__).resolve().parent.parent
MANIFEST = HOME / 'scripts/m236-candidate-elfs.tsv'
PACKAGES = HOME / 'rootfs/alpine/packages-m236-candidates.txt'
NATIVE_COUNT = 7

spec = spec_from_file_location('m236_physical_owner',
                              Path(__file__).with_name('m206-alpine-disk-check.py'))
if spec is None or spec.loader is None:
    raise RuntimeError('M236 requires the existing M206 physical-disk owner')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)


def verify_signed_inventory(port: str, partition: Path, m191, home: Path = HOME) -> dict[str, str]:
    """No APK DB claim counts unless the real ext4 ELF matches its signed APK."""
    inventory = home / f'build/external/m236-candidate-inventory/{port}/candidates.tsv'
    if inventory.is_symlink():
        raise ValueError('[M236 SIGNED SHA FAIL] symlinked signed APK census')
    with inventory.open(newline='', encoding='utf-8') as stream:
        rows = list(csv.reader(stream, delimiter='\t'))
    if not rows or rows[0] != ['package', 'elf_path', 'status', 'sha256']:
        raise ValueError('[M236 SIGNED SHA FAIL] missing original APK inventory header')
    expected = {(r[0], r[1]) for r in csv.reader(
        (ln for ln in MANIFEST.read_text().splitlines()[1:]
         if ln.strip() and not ln.startswith('#')), delimiter='\t')}
    if len(expected) != NATIVE_COUNT:
        raise ValueError('[M236 SIGNED SHA FAIL] invalid original ELF manifest size')
    matched: set[tuple[str, str]] = set()
    signed_hashes: set[str] = set()
    verified_sha: dict[str, str] = {}
    for row in rows[1:]:
        if len(row) != 4:
            raise ValueError('[M236 SIGNED SHA FAIL] invalid signed APK census row')
        pkg, path, status, sha = row
        key = (pkg, path)
        if (status != 'NATIVE_ELF' or key not in expected or key in matched or
                not re.fullmatch(r'[0-9a-f]{64}', sha)):
            raise ValueError('[M236 SIGNED SHA FAIL] missing/duplicate/invalid signed ELF ' + path)
        actual = m191.debugfs_cat(partition, '/' + path)
        if hashlib.sha256(actual).hexdigest() != sha:
            raise ValueError('[M236 SIGNED SHA FAIL] physical ext4 differs from signed APK ' + path)
        matched.add(key)
        signed_hashes.add(sha)
        verified_sha[path] = sha
    if matched != expected or len(signed_hashes) != NATIVE_COUNT:
        raise ValueError('[M236 SIGNED SHA FAIL] not seven distinct authentic native ELF hashes')
    return verified_sha


def verify_semantics(partition: Path, m191) -> None:
    def read(path: str) -> bytes:
        return m191.debugfs_cat(partition, '/root/m236-state/' + path)

    def result(case: str, phase: str, name: str) -> bytes:
        return read(f'{case}.{phase}/{name}')

    fossil_payload = b'M236_REAL_FOSSIL_COMMITTED_DATA_2026\n'
    if (read('fossil-work/payload.txt') != fossil_payload or
            not read('history.fossil').startswith(b'SQLite format 3\x00')):
        raise ValueError('[M236 SEMANTIC FAIL] genuine persisted fossil repository missing')
    codec_payload = b'M236_LIBDEFLATE_FIRST_EXT4_2026\nPAYLOAD=alpha,beta,gamma\n'
    if read('codec.txt') != codec_payload:
        raise ValueError('[M236 SEMANTIC FAIL] original libdeflate input absent')
    import gzip
    if gzip.decompress(read('codec.gz')) != codec_payload:
        raise ValueError('[M236 SEMANTIC FAIL] independent host gzip decode mismatch')

    for stem, length, marker in (
        ('prefix', None, b'M236_REAL_DFU_PREFIX_FIRMWARE_2026\n'),
        ('suffix', 16, b'M236_REAL_DFU_SUFFIX_FIRMWARE_2026\n'),
    ):
        original = read(f'firmware-{stem}.raw')
        added = read(f'firmware-{stem}.added')
        if not original.startswith(marker) or added == original or not added:
            raise ValueError(f'[M236 SEMANTIC FAIL] DFU {stem} add did not modify firmware')
        if length is not None:
            if len(added) != len(original) + length:
                raise ValueError('[M236 SEMANTIC FAIL] expected exact 16-byte DFU suffix')
            extra = added[-16:]
            if (extra[8:11] != b'UFD' or extra[11] != 16 or
                    struct.unpack_from('<H', extra, 2)[0] != 0x4567 or
                    struct.unpack_from('<H', extra, 4)[0] != 0x0123):
                raise ValueError('[M236 SEMANTIC FAIL] DFU suffix VID/PID/marker invalid')
            # dfu-util stores unfinalized CRC32, initialized with all ones.
            # Python's zlib.crc32 exposes the complemented value.
            recorded_crc = struct.unpack_from('<I', extra, 12)[0]
            expected_crc = (~zlib.crc32(added[:-4])) & 0xffffffff
            if recorded_crc != expected_crc:
                raise ValueError('[M236 SEMANTIC FAIL] independently computed DFU CRC mismatch')
        elif (len(added) != len(original) + 8 or
              added[:4] != b'\x01\x00\x04\x00' or
              struct.unpack_from('<I', added, 4)[0] != len(original) or
              added[8:] != original):
            raise ValueError('[M236 SEMANTIC FAIL] authentic Stellaris prefix/4KiB address missing')
        case = ('dfu-stellaris-prefix-roundtrip' if stem == 'prefix' else
                'dfu-suffix-crc-roundtrip')
        for phase in ('first', 'reopen'):
            if result(case, phase, 'after-add.dfu') != original:
                raise ValueError(f'[M236 SEMANTIC FAIL] {stem} {phase} removal failed to restore payload')
            if not result(case, phase, 'check.log'):
                raise ValueError(f'[M236 SEMANTIC FAIL] {stem} {phase} native DFU check absent')

    arc_a = b'M236_REAL_OFARC_ALPHA\n'
    arc_b = b'M236_REAL_OFARC_BETA\n'
    if read('archive-src/alpha.txt') != arc_a or read('archive-src/beta.txt') != arc_b:
        raise ValueError('[M236 SEMANTIC FAIL] first-QEMU archive input absent')
    import io
    import zipfile
    with zipfile.ZipFile(io.BytesIO(read('real-ofarc.zip'))) as bundle:
        if bundle.read('alpha.txt') != arc_a or bundle.read('beta.txt') != arc_b:
            raise ValueError('[M236 SEMANTIC FAIL] independently decoded original ZIP differs')

    h = hashlib.sha256(arc_a).hexdigest()
    for phase in ('first', 'reopen'):
        if (result('fossil-persisted-commit', phase, 'checked-out.txt') != fossil_payload or
                b'M236 durable original fossil check-in' not in
                result('fossil-persisted-commit', phase, 'timeline.txt')):
            raise ValueError(f'[M236 SEMANTIC FAIL] {phase} fossil authentic check-in absent')
        if result('libdeflate-gzip-roundtrip', phase, 'restored.txt') != codec_payload:
            raise ValueError(f'[M236 SEMANTIC FAIL] {phase} actual libdeflate-gunzip differs')
        if (result('ofarc-zip-recovery', phase, 'alpha.txt') != arc_a or
                result('ofarc-zip-recovery', phase, 'beta.txt') != arc_b or
                b'alpha.txt' not in result('ofarc-zip-recovery', phase, 'list.log')):
            raise ValueError(f'[M236 SEMANTIC FAIL] {phase} actual ofarc extraction differs')
        if (result('ofhash-independent-digest', phase, 'expected.sha256').strip() !=
                h.encode() or h.encode() not in
                result('ofhash-independent-digest', phase, 'ofhash.txt').lower()):
            raise ValueError(f'[M236 SEMANTIC FAIL] {phase} ofhash cryptographic digest differs')
        if b'OK' not in result('xmlsec-hmac-signature', phase, 'verified.log'):
            raise ValueError(f'[M236 SEMANTIC FAIL] {phase} real XML signature verify missing')
        tampered = result('xmlsec-hmac-signature', phase, 'tampered.xml')
        if b'M236_TAMPERED_SIGNED_XML_DOCUMENT_2026' not in tampered:
            raise ValueError(f'[M236 SEMANTIC FAIL] {phase} signed-XML tampering absent')

    signed = read('signed.xml')
    template = read('template.xml')
    if b'M236_REAL_SIGNED_XML_DOCUMENT_2026' not in signed or signed == template:
        raise ValueError('[M236 SEMANTIC FAIL] XML signature not truly produced')
    root = ET.fromstring(signed)
    ns = '{http://www.w3.org/2000/09/xmldsig#}'
    sig = root.find('.//' + ns + 'Signature')
    if sig is None or not sig.findtext(ns + 'SignatureValue'):
        raise ValueError('[M236 SEMANTIC FAIL] real XMLDSig signature value absent')
    signed_info = sig.find(ns + 'SignedInfo')
    if signed_info is None or not signed_info.findtext('.//' + ns + 'DigestValue'):
        raise ValueError('[M236 SEMANTIC FAIL] real XMLDSig digest absent')


def verify(port: str, signed_root: Path, disk: Path) -> None:
    owner.verify(port, signed_root, disk,
                 candidates=MANIFEST, packages=PACKAGES,
                 expected_count=NATIVE_COUNT, label='M236')
    m197 = owner.load('m197-alpine-disk-check.py', 'm236_m197')
    m191 = m197.load('m195-alpine-disk-check.py', 'm236_m195').load_m191()
    with tempfile.TemporaryDirectory(prefix='m236-independent-physical-ext4-') as work:
        part = Path(work) / 'root.ext4'
        m191.extract_partition(disk.resolve(strict=True), part)
        verified_elf_sha = verify_signed_inventory(port, part, m191)
        verify_semantics(part, m191)
        # This artifact is written only AFTER the complete physical disk
        # verifier. Capture the actual SHA for two independent QEMU output
        # witnesses per original ELF, not a synthetic preseeded PASS marker.
        outputs = {
            'fossil-persisted-commit': 'checked-out.txt',
            'libdeflate-gzip-roundtrip': 'restored.txt',
            'dfu-stellaris-prefix-roundtrip': 'after-add.dfu',
            'dfu-suffix-crc-roundtrip': 'after-add.dfu',
            'ofarc-zip-recovery': 'alpha.txt',
            'ofhash-independent-digest': 'ofhash.txt',
            'xmlsec-hmac-signature': 'verified.log',
        }
        output_sha = {
            f'{case}.{phase}/{file}': hashlib.sha256(m191.debugfs_cat(
                part, f'/root/m236-state/{case}.{phase}/{file}')).hexdigest()
            for case, file in outputs.items() for phase in ('first', 'reopen')
        }
        part_sha = hashlib.sha256()
        with part.open('rb') as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                part_sha.update(chunk)
        proof = {
            'milestone': 'M236', 'port': port,
            'signed_original_elf_count': NATIVE_COUNT,
            'signed_original_elf_sha256': verified_elf_sha,
            'first_reopen_physical_output_sha256': output_sha,
            'ext4_partition_sha256': part_sha.hexdigest(),
            'oracle': 'original-signed-APK-SHA-equals-two-boot-physical-ext4-ELF-and-real-bytes',
        }
    (disk.parent / 'm236-physical-ext4-evidence.json').write_text(
        json.dumps(proof, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print('[M236 PHYSICAL DISK PASS] 7 distinct original signed APK native ELFs match '
          'all inventory SHA, both QEMUs wrote strict real functionality to one physical ext4')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('usage: m236-alpine-disk-check.py PORT SIGNED_ROOT DISK')
    verify(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]))
