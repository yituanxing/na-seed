#!/usr/bin/env python3
"""M224 original signed Nmap/iperf3/Ncat and physical first+reopen ext4.

Reuse the M206 disk owner's real GPT/ext4, APK world, installed database,
per-ISA ELF, public HTTPS and fsck checks. M224 adds exact signed-census ELF
hashes and actual Ncat source-byte persistence, not a guest UART-only PASS.
"""

import csv
import hashlib
from importlib.util import module_from_spec, spec_from_file_location
import json
import math
from pathlib import Path
import re
import sys
import tempfile


def load(name: str, alias: str):
    target = Path(__file__).with_name(name)
    spec = spec_from_file_location(alias, target)
    if spec is None or spec.loader is None:
        raise RuntimeError('M224 missing required established oracle: ' + name)
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


owner = load('m206-alpine-disk-check.py', 'm224_m206_disk')
host = load('m224-tcp-server.py', 'm224_source_generator')
BINARIES = ('/usr/bin/nmap', '/usr/bin/iperf3', '/usr/bin/ncat')
CASES = {
    'nmap': 'real-host-nmap-connect',
    'iperf3': 'real-host-iperf3-throughput',
    'ncat': 'real-host-ncat-random',
}


def staging_check(port: str, signed_root: Path) -> None:
    m191 = owner.load('m191-alpine-root-disk.py', 'm224_stage_m191')
    if port not in m191.ARCH_MACHINE:
        raise ValueError('[M224 STAGE FAIL] unsupported target ISA')
    signed_root = signed_root.resolve(strict=True)
    for path in BINARIES:
        # Inspect the basename BEFORE symlink resolution: no preinstalled ELF
        # or BusyBox applet alias may masquerade as original signed APK.
        raw = m191.guest_path(signed_root, str(Path(path).parent)) / Path(path).name
        if raw.exists() or raw.is_symlink():
            raise ValueError('[M224 STAGE FAIL] signed candidate preseeded: ' + path)


def source_bytes(phase: str, host_dir: Path) -> bytes:
    return host.source_bytes(phase, host_dir)


def iperf3_bytes(value: bytes, phase: str) -> int:
    try:
        report = json.loads(value)
        if not isinstance(report, dict) or report.get('error'):
            raise ValueError('guest iperf3 report failed')
        end = report['end']
        sent = end['sum_sent']
        received = end['sum_received']
        if (type(sent['bytes']) is not int or sent['bytes'] < 512 or
                type(received['bytes']) is not int or received['bytes'] < 1):
            raise ValueError('guest did not report real TCP byte delivery')
        if not isinstance(sent['bits_per_second'], (int, float)) or \
                not math.isfinite(sent['bits_per_second']) or sent['bits_per_second'] <= 0:
            raise ValueError('guest did not report throughput')
        return sent['bytes']
    except (KeyError, TypeError, json.JSONDecodeError, ValueError) as exc:
        raise ValueError(f'[M224 DISK FAIL] guest iperf3 {phase} JSON has no real transfer') from exc


def iperf3_physical_diagnostic(partition: Path, m191,
                               phase: str, value: bytes) -> str:
    """Failure-only original ELF output evidence; never grants acceptance."""
    name = f'/root/m224-work/{CASES["iperf3"]}.{phase}/iperf3.err'
    stderr = m191.debugfs_cat(partition, name)
    try:
        decoded = json.loads(value)
        shape = ({'keys': sorted(decoded), 'error': decoded.get('error'),
                  'end_keys': sorted(decoded.get('end', {}))}
                 if isinstance(decoded, dict) else {'type': type(decoded).__name__})
    except (ValueError, TypeError) as exc:
        shape = {'json_parse_error': str(exc)}
    return (f'phase={phase!r} json_size={len(value)} '
            f'json_sha256={hashlib.sha256(value).hexdigest()} '
            f'json_shape={shape!r} json_start={value[:1200]!r} '
            f'json_end={value[-600:]!r} stderr={stderr[:900]!r}')


def nmap_port_contrast(content: bytes) -> bool:
    """Require the exact two original Nmap result lines, on physical bytes."""
    return bool(re.search(rb'(?m)^15441/tcp\s+open\b', content) and
                re.search(rb'(?m)^15442/tcp\s+closed\b', content))


def nmap_physical_diagnostic(partition: Path, name: str,
                             content: bytes) -> str:
    """Failure-only forensic evidence; never changes the acceptance oracle."""
    import subprocess

    state = subprocess.run(['debugfs', '-R', 'stat ' + name, str(partition)],
                           check=False, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)
    directory = subprocess.run(['debugfs', '-R', 'ls -l ' + str(Path(name).parent),
                                 str(partition)], check=False, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
    m191 = owner.load('m191-alpine-root-disk.py', 'm224_diag_m191')
    nmap_stderr = m191.debugfs_cat(partition, str(Path(name).with_name('nmap.err')))
    ledger = m191.debugfs_cat(partition, '/root/m224-first-results.tsv')
    return (f'path={name!r} size={len(content)} '
            f'sha256={hashlib.sha256(content).hexdigest()} '
            f'first256={content[:256]!r} '
            f'err={nmap_stderr[:512]!r} ledger={ledger[:900]!r} '
            f'inode_stat={state.stdout[:900]!r} directory={directory.stdout[:800]!r}')


def physical_outputs(partition: Path, m191, first_dir: Path,
                     reopen_dir: Path) -> dict:
    sources = []
    transfer = {}
    for phase, host_dir in (('first', first_dir), ('reopen', reopen_dir)):
        source = source_bytes(phase, host_dir)
        guest_random = m191.debugfs_cat(
            partition, f'/root/m224-work/{CASES["ncat"]}.{phase}/host-random.bin')
        if guest_random != source:
            raise ValueError('[M224 DISK FAIL] native Ncat physical ext4 random response '
                             'differs from independent host')
        sources.append(source)
        nmap = m191.debugfs_cat(partition,
                                f'/root/m224-work/{CASES["nmap"]}.{phase}/nmap.txt')
        if not nmap_port_contrast(nmap):
            name = f'/root/m224-work/{CASES["nmap"]}.{phase}/nmap.txt'
            raise ValueError('[M224 DISK FAIL] native Nmap physical port contrast absent: '
                             + nmap_physical_diagnostic(partition, name, nmap))
        result = m191.debugfs_cat(partition,
                                  f'/root/m224-work/{CASES["iperf3"]}.{phase}/iperf3.json')
        try:
            transfer[phase] = iperf3_bytes(result, phase)
        except ValueError as exc:
            raise ValueError(str(exc) + ': ' +
                             iperf3_physical_diagnostic(partition, m191,
                                                        phase, result)) from exc
    if sources[0] == sources[1]:
        raise ValueError('[M224 DISK FAIL] first/reopen share the same host random source')
    return {'ncat_source_sha256': {phase: hashlib.sha256(data).hexdigest()
                                   for phase, data in zip(('first', 'reopen'), sources)},
            'native_iperf3_sent_bytes': transfer,
            'nmap_tcp_ports': {'open': 15441, 'closed': 15442}}


def signed_inventory_sha(port: str, partition: Path, m191, root: Path) -> None:
    """Bind every physical native ELF SHA to exact official signed APK census."""
    table = root / f'build/external/m224-candidate-inventory/{port}/candidates.tsv'
    if table.is_symlink():
        raise ValueError('[M224 SHA FAIL] symlinked original signed inventory')
    with table.open(newline='', encoding='utf-8') as stream:
        rows = list(csv.reader(stream, delimiter='\t'))
    if not rows or rows[0] != ['package', 'elf_path', 'status', 'sha256']:
        raise ValueError('[M224 SHA FAIL] missing original signed inventory header')
    expected = {(record[0], record[1]) for record in
                (line.split('\t') for line in
                 (root / 'scripts/m224-candidate-elfs.tsv').read_text().splitlines()[1:])
                if len(record) == 4}
    if len(expected) != 3:
        raise ValueError('[M224 SHA FAIL] invalid expected original signed candidate set')
    seen = set()
    shas = set()
    for row in rows[1:]:
        if len(row) != 4:
            raise ValueError('[M224 SHA FAIL] invalid signed inventory row')
        package, path, status, fingerprint = row
        pair = (package, path)
        if pair not in expected or pair in seen or status != 'NATIVE_ELF' or \
                not re.fullmatch(r'[0-9a-f]{64}', fingerprint):
            raise ValueError('[M224 SHA FAIL] malformed original signed candidate census')
        if hashlib.sha256(m191.debugfs_cat(partition, '/' + path)).hexdigest() != fingerprint:
            raise ValueError('[M224 SHA FAIL] physical ELF differs from original signed APK ' + path)
        seen.add(pair)
        shas.add(fingerprint)
    if seen != expected or len(shas) != 3:
        raise ValueError('[M224 SHA FAIL] missing/duplicate signed original ELF')


def verify(port: str, signed_root: Path, disk: Path,
           first_dir: Path, reopen_dir: Path) -> None:
    staging_check(port, signed_root)
    home = Path(__file__).resolve().parent.parent
    owner.verify(port, signed_root, disk,
                 candidates=home / 'scripts/m224-candidate-elfs.tsv',
                 packages=home / 'rootfs/alpine/packages-m224-candidates.txt',
                 expected_count=3, label='M224')
    m191 = owner.load('m191-alpine-root-disk.py', 'm224_physical_m191')
    with tempfile.TemporaryDirectory(prefix='m224-physical-tcp-') as tmp:
        partition = Path(tmp) / 'root.ext4'
        m191.extract_partition(disk.resolve(strict=True), partition)
        physical_outputs(partition, m191, first_dir, reopen_dir)
        signed_inventory_sha(port, partition, m191, home)
    print('[M224 ORIGINAL TCP DISK PASS] three exact signed native ELF SHA, '
          'APK world+DB/fsck, first/reopen host-random Ncat ext4 bytes, '
          'Nmap open/closed, original iperf3 throughput')


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == 'stage':
        staging_check(sys.argv[2], Path(sys.argv[3]))
        print('[M224 SIGNED STAGE PASS] no original ELF/BusyBox alias preseeded')
    elif len(sys.argv) == 7 and sys.argv[1] == 'verify':
        verify(sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
               Path(sys.argv[5]), Path(sys.argv[6]))
    else:
        raise SystemExit('usage: m224-alpine-disk-check.py stage ISA ROOT | '
                         'verify ISA ROOT DISK HOST_FIRST HOST_REOPEN')
