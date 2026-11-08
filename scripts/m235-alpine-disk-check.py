#!/usr/bin/env python3
"""M235 physical disk oracle: signed ELF plus all 12 concrete data witnesses.

Every first/reopen file is read independently from the *same disk partition*,
after the native second QEMU has finished, not from guest UART or staging.
"""
from importlib.util import module_from_spec, spec_from_file_location
import base64
import hashlib
import json
from pathlib import Path
import re
import sys
import tempfile

source = Path(__file__).with_name('m206-alpine-disk-check.py')
spec = spec_from_file_location('m235_disk_owner', source)
if spec is None or spec.loader is None:
    raise RuntimeError('M235 requires existing physical ext4 oracle')
owner = module_from_spec(spec)
spec.loader.exec_module(owner)


def validate_persistent_data(port: str, disk: Path) -> None:
    m191 = owner.load('m191-alpine-root-disk.py', 'm235_disk_m191')
    if port not in m191.ARCH_MACHINE:
        raise ValueError('[M235 DISK FAIL] unsupported ISA')
    with tempfile.TemporaryDirectory(prefix='m235-original-elf-data-') as tmp:
        partition = Path(tmp) / 'root.ext4'
        m191.extract_partition(disk.resolve(strict=True), partition)
        read = lambda path: m191.debugfs_cat(partition, '/root/m235-state/' + path)

        # The first QEMU created these exact data records; the second cannot
        # silently regenerate them and claim durable restoration.
        pairs = b'M235_DB_KEY_ONE\nM235_DB_VALUE_ONE\nM235_DB_KEY_TWO\nM235_DB_VALUE_TWO\n'
        if read('pairs.txt') != pairs or len(read('original.db')) < 512:
            raise ValueError('[M235 DISK FAIL] original Berkeley DB physical data absent')
        original_bytes = read('original.db')
        if original_bytes[:4] == b'M235':
            raise ValueError('[M235 DISK FAIL] fake plaintext DB database')
        source = b'{ name: "NaSeed", slots: [x * 3 for x in [2, 4]], enabled: true }\n'
        if read('config.jsonnet') != source:
            raise ValueError('[M235 DISK FAIL] original persisted Jsonnet missing')

        expected_json = {'name': 'NaSeed', 'slots': [6, 12], 'enabled': True}
        for phase in ('first', 'reopen'):
            def phase_output(case: str, name: str) -> bytes:
                return read(f'{case}.{phase}/{name}')

            for command, name in (
                ('bdb-import-records', 'read.dump'),
                ('bdb-dump-records', 'read.dump'),
                ('bdb-verify-persisted', 'read.dump'),
                ('bdb-btree-record-stats', 'read.dump'),
                ('bdb-btree-tune-analysis', 'read.dump'),
                ('bdb-upgrade-data-preserve', 'read.dump'),
                ('bdb-upgrade-data-preserve', 'upgraded.dump'),
            ):
                actual = phase_output(command, name)
                if any(token not in actual for token in
                       (b'M235_DB_KEY_ONE', b'M235_DB_VALUE_ONE',
                        b'M235_DB_KEY_TWO', b'M235_DB_VALUE_TWO')):
                    raise ValueError(f'[M235 DISK FAIL] {phase} original {command} actual records mismatch')
            if phase == 'reopen':
                newer = phase_output('bdb-import-records', 'reimport.dump')
                if b'M235_DB_VALUE_TWO' not in newer:
                    raise ValueError('[M235 DISK FAIL] second QEMU Berkeley DB reimport absent')
            if not re.search(rb'(number of (unique )?keys|btree|page size)',
                             phase_output('bdb-btree-record-stats', 'stat.txt'), re.I):
                raise ValueError('[M235 DISK FAIL] real Berkeley DB stat absent')
            if not re.search(rb'page[- ]?size',
                             phase_output('bdb-btree-tune-analysis', 'tuner.txt'), re.I):
                raise ValueError('[M235 DISK FAIL] real DB tuner analysis absent')
            for cmd, filename in (
                ('jsonnet-evaluate-object', 'actual.json'),
                ('jsonnet-format-roundtrip', 'expected.json'),
                ('jsonnet-format-roundtrip', 'formatted.json'),
            ):
                if json.loads(phase_output(cmd, filename)) != expected_json:
                    raise ValueError(f'[M235 DISK FAIL] {phase} Jsonnet executed value mismatch')
            cidr = phase_output('cidr-network-broadcast', 'cidr.txt')
            if b'198.51.100.64' not in cidr or b'198.51.100.95' not in cidr:
                raise ValueError(f'[M235 DISK FAIL] {phase} IPv4 CIDR arithmetic mismatch')

        # Require actual native Git LFS filter's object, pointer and both
        # independently smudged payloads, with independently computed SHA.
        payload = read('lfs-repo/payload.bin')
        digest = hashlib.sha256(payload).hexdigest()
        pointer = read('lfs-repo/payload.pointer')
        if (payload != b'M235_SIGNED_GIT_LFS_DURABLE_PAYLOAD_2026\n' or
                not pointer.startswith(b'version https://git-lfs.github.com/spec/v1\n') or
                f'oid sha256:{digest}\n'.encode() not in pointer or
                f'size {len(payload)}\n'.encode() not in pointer):
            raise ValueError('[M235 DISK FAIL] actual Git LFS pointer payload SHA/size differs')
        if read(f'lfs-repo/.git/lfs/objects/{digest[:2]}/{digest[2:4]}/{digest}') != payload:
            raise ValueError('[M235 DISK FAIL] original Git LFS content-addressable blob absent')
        for phase in ('first', 'reopen'):
            if read(f'git-lfs-pointer-roundtrip.{phase}/restored.bin') != payload:
                raise ValueError(f'[M235 DISK FAIL] {phase} Git LFS smudge mismatch')

        private = read('wg.private').strip()
        public = read('wg.public').strip()
        try:
            private_raw = base64.b64decode(private, validate=True)
            public_raw = base64.b64decode(public, validate=True)
        except ValueError as exc:
            raise ValueError('[M235 DISK FAIL] invalid native WireGuard keys') from exc
        if len(private_raw) != 32 or len(public_raw) != 32 or private_raw == public_raw:
            raise ValueError('[M235 DISK FAIL] malformed WireGuard X25519 key pair')
        for phase in ('first', 'reopen'):
            if read(f'wireguard-key-derivation.{phase}/derived.public').strip() != public:
                raise ValueError(f'[M235 DISK FAIL] {phase} original WireGuard derivation mismatch')

        # xh must receive distinct 192-byte unpredictably generated HOST data
        # over two separate native-QEMU TCP transactions; M235's per-phase
        # HTTP oracle also checks complete Ethernet PCAP GET/200/full entity.
        previous = None
        for phase in ('first', 'reopen'):
            witness = disk.parent / f'm223-host-{phase}/wget.source'
            if witness.is_symlink():
                raise ValueError('[M235 DISK FAIL] host HTTP witness symlink')
            source = witness.read_bytes()
            if (not re.fullmatch(rb'M223_' + phase.upper().encode() +
                                 rb'_WGET_SOURCE_[0-9a-f]{192}\n', source) or
                    source == previous or read(f'xh.{phase}.body') != source or
                    read(f'http-response-body.{phase}/download.body') != source):
                raise ValueError(f'[M235 DISK FAIL] {phase} host-random HTTP ext4 mismatched')
            previous = source


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit('usage: m235-alpine-disk-check.py PORT SIGNED_ROOT DISK')
    base = Path(__file__).resolve().parent.parent
    owner.verify(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]),
                 candidates=base / 'scripts/m235-candidate-elfs.tsv',
                 packages=base / 'rootfs/alpine/packages-m235-candidates.txt',
                 expected_count=12, label='M235',
                 # Base Alpine exposes ipcalc as a BusyBox applet symlink.
                 # The shared M206 verifier accepts this exact pre-APK alias
                 # ONLY when it resolves to the ISA-matched original BusyBox;
                 # an actual preinstalled independent ELF still fails.
                 allowed_staged_busybox_aliases=('/bin/ipcalc',))
    validate_persistent_data(sys.argv[1], Path(sys.argv[3]))
    print('[M235 PHYSICAL DISK PASS] 12 distinct original native ELF hashes, '
          'six signed APK identities, two-boot real DB/JSON/IP/WG/LFS/HTTP '
          'persisted values and original M197 data')


if __name__ == '__main__':
    main()
