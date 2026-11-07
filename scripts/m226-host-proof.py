#!/usr/bin/env python3
"""Native QEMU Ethernet+true upstream rest-server+physical ext4 REST proof.

This never substitutes source GET/PUT for a restic repository. The actual
official rest-server must show protocol config/keys/data/snapshots transactions;
the independent QEMU TCP flow must match. Encrypted restic packs deliberately
are NOT compared against source plaintext. Original restored bytes are checked
on the physical guest ext4 and independently by host-native restic.
"""

import hashlib
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path
import re
import sys
import tempfile


def load(filename, alias):
    spec = spec_from_file_location(alias, Path(__file__).with_name(filename))
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


origin = load('m226-host-source.py', 'm226_host_challenge')
network = load('m225-host-proof.py', 'm226_original_tcp_flows')
disklib = load('m191-alpine-root-disk.py', 'm226_ext4')
PORT_REST, PORT_ORIGIN = 15452, 15453


def body(phase: str, directory: Path) -> bytes:
    return origin.source(phase, directory)


def two_phase_sources(first: Path, reopen: Path):
    one, two = body('first', first), body('reopen', reopen)
    if len(one) == len(two) or one == two:
        raise ValueError('[M226 FAIL] source nonce not phase-unique')


def physical_files(phase: str, origin_dir: Path, read):
    src = body(phase, origin_dir)
    checks = [f'/root/m226-work/{phase}/source.bin',
              f'/root/m226-work/{phase}/restore-current/root/m226-work/{phase}/source.bin']
    if phase == 'reopen':
        checks.extend(['/root/m226-work/first/source.bin',
                       '/root/m226-work/reopen/restore-first/root/m226-work/first/source.bin'])
    previous = body('first', origin_dir.with_name('m226-host-first')) if phase == 'reopen' else src
    for number, path in enumerate(checks):
        expected = previous if phase == 'reopen' and number >= 2 else src
        if read(path) != expected:
            raise ValueError('[M226 DISK FAIL] physical ext4 source/remote restore mismatch: ' + path)
    for log in ('backup', 'snapshots', 'restore-current', 'check'):
        data = read(f'/root/m226-work/{phase}/{log}.log')
        if len(data) == 0 or len(data) > 250000:
            raise ValueError('[M226 DISK FAIL] missing real original restic CLI output: ' + log)
    if phase == 'reopen':
        if not read('/root/m226-work/reopen/preexisting-snapshots.log'):
            raise ValueError('[M226 DISK FAIL] reopened repository snapshot proof missing')
        if not read('/root/m226-work/reopen/restore-first.log'):
            raise ValueError('[M226 DISK FAIL] reopened first backup restore proof missing')
    else:
        if not read('/root/m226-work/first/init.log'):
            raise ValueError('[M226 DISK FAIL] first boot restic init proof missing')


def rest_server_requests(phase: str, log: str) -> dict:
    requests = re.findall(
        r'\"(GET|POST|HEAD|DELETE|PUT) (/repo/[^ ]*) HTTP/1\.[01]\" (\d{3})', log)
    if len(requests) < 12:
        raise ValueError('[M226 HOST FAIL] no real restic repository transactions')
    allow = {200, 201, 204, 206}
    if any(int(code) not in allow and not (
            phase == 'first' and method == 'HEAD' and path == '/repo/config' and code == '404')
           for method, path, code in requests):
        raise ValueError('[M226 HOST FAIL] rest-server has non-success operations')
    if phase == 'first':
        if requests.count(('POST', '/repo/?create=true', '200')) != 1:
            raise ValueError('[M226 HOST FAIL] real REST init POST create=true absent')
    elif any(path == '/repo/?create=true' for _method, path, _code in requests):
        raise ValueError('[M226 HOST FAIL] second boot reinitialized old repository')
    # These are the actual encrypted restic layout, not a toy HTTP echo.
    required = {
        'GET': ('/repo/config', '/repo/keys/', '/repo/snapshots/'),
        'POST': ('/repo/data/', '/repo/snapshots/'),
    }
    for method, prefixes in required.items():
        for prefix in prefixes:
            if not any(action == method and path.startswith(prefix)
                       for action, path, _code in requests):
                raise ValueError(f'[M226 HOST FAIL] real restic {method} {prefix} absent')
    return {'rest_requests': len(requests),
            'rest_uploads': sum(action == 'POST' for action, _path, _code in requests)}


def real_wire(phase: str, capture: bytes, challenge: bytes) -> dict:
    live = network.flows(capture, {PORT_REST, PORT_ORIGIN})
    source = [flow for flow in live if flow['port'] == PORT_ORIGIN]
    if not any(b'GET /m226.source HTTP/1.' in flow['client'] and
               b'HTTP/1.' in flow['server'] and b' 200 ' in flow['server'] and
               flow['server'].split(b'\r\n\r\n', 1)[-1] == challenge
               for flow in source):
        raise ValueError('[M226 WIRE FAIL] native TCP source HTTP 200/body absent')
    requests = b'\r\n'.join(flow['client'] for flow in live if flow['port'] == PORT_REST)
    responses = b'\r\n'.join(flow['server'] for flow in live if flow['port'] == PORT_REST)
    if (b'GET /repo/config HTTP/1.' not in requests or
            b'GET /repo/snapshots/ HTTP/1.' not in requests or
            b'POST /repo/data/' not in requests or
            b'POST /repo/snapshots/' not in requests or
            not re.search(rb'HTTP/1\.[01] 20[06] ', responses)):
        raise ValueError('[M226 WIRE FAIL] real encrypted repository REST TCP absent')
    if phase == 'first':
        if b'POST /repo/?create=true HTTP/1.' not in requests:
            raise ValueError('[M226 WIRE FAIL] actual guest restic init POST absent')
    elif b'POST /repo/?create=true' in requests:
        raise ValueError('[M226 WIRE FAIL] second guest boot reinitialized repository')
    return {'native_tcp_connections': len(live),
            'native_repository_tcp': sum(flow['port'] == PORT_REST for flow in live),
            'source_sha256': hashlib.sha256(challenge).hexdigest()}


def repo_files(repo_storage: Path, phase: str) -> dict:
    repo = repo_storage / 'repo'
    if repo.is_symlink() or not (repo / 'config').is_file():
        raise ValueError('[M226 HOST FAIL] official rest-server real repository config absent')
    counts = {}
    for dirname in ('keys', 'data', 'snapshots', 'index'):
        folder = repo / dirname
        if folder.is_symlink() or not folder.is_dir():
            raise ValueError('[M226 HOST FAIL] actual restic repository directory absent: ' + dirname)
        content = list(folder.rglob('*'))
        if any(entry.is_symlink() for entry in content):
            raise ValueError('[M226 HOST FAIL] symlink in true restic repository')
        counts[dirname] = len([entry for entry in content if entry.is_file()])
        if counts[dirname] < (2 if phase == 'reopen' and dirname == 'snapshots' else 1):
            raise ValueError('[M226 HOST FAIL] insufficient encrypted restic objects: ' + dirname)
    return counts


def verify(phase, isa, pcap_file, source_log, rest_log, origin_dir, repo_dir, disk, output):
    if phase not in ('first', 'reopen'):
        raise ValueError('[M226 FAIL] unknown first/reopen phase')
    challenge = body(phase, origin_dir)
    lines = source_log.read_text().splitlines()
    expected = f'M226_SOURCE_GET phase={phase} bytes={len(challenge)} sha256={hashlib.sha256(challenge).hexdigest()}'
    if (lines.count(f'[M226 SOURCE READY] phase={phase} port={PORT_ORIGIN}') != 1 or
            lines.count(expected) != 1):
        raise ValueError('[M226 HOST FAIL] missing/duplicate real host source transfer receipt')
    parsed = rest_server_requests(phase, rest_log.read_text())
    parsed.update(real_wire(phase, pcap_file.read_bytes(), challenge))
    parsed['repo_objects'] = repo_files(repo_dir, phase)
    parsed['phase'] = phase
    parsed['native_isa'] = isa
    with tempfile.TemporaryDirectory(prefix='m226-physical-ext4-') as tmp:
        partition = Path(tmp) / 'partition.ext4'
        disklib.extract_partition(disk.resolve(strict=True), partition)
        physical_files(phase, origin_dir, lambda path: disklib.debugfs_cat(partition, path))
    output.write_text(json.dumps(parsed, sort_keys=True, indent=2) + '\n')
    print(f'[M226 GENUINE REST REPOSITORY PCAP EXT4 PASS] {isa} phase={phase} '
          f"requests={parsed['rest_requests']} snapshots={parsed['repo_objects']['snapshots']}")


if __name__ == '__main__':
    if len(sys.argv) != 10:
        raise SystemExit('usage: m226-host-proof.py PHASE ISA PCAP SOURCE_LOG REST_LOG '
                         'HOST_ORIGIN HOST_REPO PHYSICAL_DISK OUTPUT_JSON')
    verify(sys.argv[1], sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
           Path(sys.argv[5]), Path(sys.argv[6]), Path(sys.argv[7]),
           Path(sys.argv[8]), Path(sys.argv[9]))
