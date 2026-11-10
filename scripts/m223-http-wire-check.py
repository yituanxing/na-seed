#!/usr/bin/env python3
"""Independent real QEMU Ethernet HTTP GET/200/body, host log, disk oracle.

Actual host server + unique source files, signed original ELF guest semantics,
and the QEMU PCAP must agree. A guest UART PASS alone NEVER counts.
"""
from importlib.util import module_from_spec, spec_from_file_location
import hashlib
import ipaddress
import json
from pathlib import Path
import re
import struct
import sys
import tempfile

pcap_path = Path(__file__).with_name('m196-public-wire-check.py')
spec = spec_from_file_location('m223_m196_pcap', pcap_path)
if spec is None or spec.loader is None:
    raise RuntimeError('M223 requires established actual QEMU PCAP parser')
pcap = module_from_spec(spec)
spec.loader.exec_module(pcap)

disk_path = Path(__file__).with_name('m223-alpine-disk-check.py')
dspec = spec_from_file_location('m223_http_disk_source', disk_path)
if dspec is None or dspec.loader is None:
    raise RuntimeError('M223 requires original host-random physical-disk oracle')
disk_source = module_from_spec(dspec)
dspec.loader.exec_module(disk_source)

HOST = ipaddress.IPv4Address('10.0.2.2')
GUEST = ipaddress.IPv4Network('10.0.2.0/24')
PORT = 15431
ROUTES = ('wget', 'aria2')


def contiguous_payload(parts: list[tuple[int, bytes]]) -> bytes:
    """Discard TCP retransmission overlaps; never fabricate missing frames."""
    if not parts:
        return b''
    merged = bytearray()
    expected = None
    base = None
    for seq, data in sorted(parts, key=lambda item: (item[0], -len(item[1]))):
        if expected is None:
            base = seq
            expected = seq
        if seq > expected:
            # A gap is not evidence of an intact HTTP entity body.
            return bytes(merged)
        overlap = min(len(data), expected - seq)
        if overlap and merged[seq - base:seq - base + overlap] != data[:overlap]:
            raise ValueError('[M223 WIRE FAIL] contradictory retransmitted TCP bytes')
        trim = expected - seq
        if trim < len(data):
            merged.extend(data[trim:])
            expected += len(data) - trim
    return bytes(merged)


def real_http_flows(raw: bytes, sources: dict[str, bytes]) -> dict:
    """Two independently proven guest HTTP GET + real host 200 source bytes."""
    flows = {}
    for index, frame in enumerate(pcap.pcap_frames(raw)):
        packet = pcap.ipv4_packet(frame)
        if packet is None:
            continue
        source_ip, destination, protocol, data = packet
        if protocol != 6 or len(data) < 20:
            continue
        sport, dport, seq = struct.unpack_from('!HHI', data)
        header = (data[12] >> 4) * 4
        if header < 20 or header > len(data):
            continue
        flags = data[13]
        to_host = (source_ip in GUEST and source_ip != HOST and
                   destination == HOST and dport == PORT and sport >= 1024)
        to_guest = (source_ip == HOST and destination in GUEST and
                    destination != HOST and sport == PORT and dport >= 1024)
        if not (to_host or to_guest):
            continue
        guest_ip = str(source_ip if to_host else destination)
        guest_port = sport if to_host else dport
        flow = flows.setdefault((guest_ip, guest_port),
                                {'syn': [], 'synack': [], 'ack': [],
                                 'request': [], 'response': []})
        if to_host:
            if flags & 0x02 and not flags & 0x10:
                flow['syn'].append(index)
            if flags & 0x10 and not flags & 0x02:
                flow['ack'].append(index)
            if len(data) > header:
                flow['request'].append((seq, data[header:]))
        else:
            if flags & 0x12 == 0x12:
                flow['synack'].append(index)
            if len(data) > header:
                flow['response'].append((seq, data[header:]))
    accepted = {route: [] for route in ROUTES}
    for key, flow in flows.items():
        if not (flow['syn'] and flow['synack'] and flow['ack'] and
                min(flow['syn']) < min(flow['synack']) < min(flow['ack'])):
            continue
        request = contiguous_payload(flow['request'])
        response = contiguous_payload(flow['response'])
        for route in ROUTES:
            if (re.search(rb'(?:^|\r\n)GET /' + route.encode() +
                          rb' HTTP/1\.[01]\r\n', request) and
                    response.startswith((b'HTTP/1.1 200 ', b'HTTP/1.0 200 ')) and
                    b'\r\n\r\n' in response and
                    response.split(b'\r\n\r\n', 1)[1] == sources[route]):
                accepted[route].append({'guest': key[0], 'port': key[1],
                                        'bytes': len(sources[route])})
                break
    if any(not accepted[route] for route in ROUTES):
        raise ValueError('[M223 WIRE FAIL] original guest wget/aria2 not in real '
                         'independent TCP three-way HTTP GET/200/full-body PCAP')
    return {'pcap_real_http_sessions': {route: len(accepted[route]) for route in ROUTES},
            'pcap_guest_http_body_bytes': {route: len(sources[route]) for route in ROUTES},
            'pcap_guest_addresses': sorted({row['guest'] for flow in accepted.values() for row in flow})}


def host_log_oracle(phase: str, host_log: str, sources: dict[str, bytes]) -> dict:
    if host_log.splitlines().count(f'[M223 HOST READY] phase={phase} port={PORT}') != 1:
        raise ValueError('[M223 HOST FAIL] phase-independent server READY missing')
    lines = [line for line in host_log.splitlines() if line.startswith('M223_HOST_GET ')]
    counts = {route: 0 for route in ROUTES}
    pattern = re.compile(r'M223_HOST_GET phase=(first|reopen) path=/(wget|aria2) '
                         r'status=200 bytes=([0-9]+) sha256=([0-9a-f]{64}) peer=127\.0\.0\.1\Z')
    for line in lines:
        match = pattern.fullmatch(line)
        if match is None or match.group(1) != phase:
            raise ValueError('[M223 HOST FAIL] malformed/foreign HTTP receipt')
        _, route, size, sha = match.groups()
        body = sources[route]
        if int(size) != len(body) or sha != hashlib.sha256(body).hexdigest():
            raise ValueError('[M223 HOST FAIL] actual HTTP GET differs from source bytes')
        counts[route] += 1
    if any(counts[route] < 1 for route in ROUTES):
        raise ValueError('[M223 HOST FAIL] independent host did not serve BOTH signed clients')
    return {'host_real_http_get_count': counts,
            'host_source_sha256': {route: hashlib.sha256(sources[route]).hexdigest()
                                   for route in ROUTES}}


def physical_downloads(port: str, disk: Path, phase: str,
                       sources: dict[str, bytes]) -> dict:
    m191 = disk_source.owner.load('m191-alpine-root-disk.py', 'm223_wire_m191')
    if port not in m191.ARCH_MACHINE:
        raise ValueError('[M223 HOST FAIL] unsupported actual ISA')
    with tempfile.TemporaryDirectory(prefix='m223-real-http-ext4-') as tmp:
        partition = Path(tmp) / 'root.ext4'
        m191.extract_partition(disk.resolve(strict=True), partition)
        for route in ROUTES:
            guest = (f'/root/m223-work/{disk_source.CASES[route]}.{phase}/download.bin')
            if m191.debugfs_cat(partition, guest) != sources[route]:
                raise ValueError(f'[M223 DISK FAIL] original {route} native ELF '
                                 f'first/reopen ext4 bytes differ from host')
        local = f'/root/m223-work/real-loopback-lighttpd.{phase}'
        expected = f'M223_LIGHTTPD_LOCAL_{phase}_HTTP_GET\n'.encode()
        if (m191.debugfs_cat(partition, local + '/loopback.txt') != expected or
                m191.debugfs_cat(partition, local + '/www/index.txt') != expected):
            raise ValueError('[M223 DISK FAIL] signed lighttpd/wget loopback absent')
    return {'physical_ext4_host_random_downloads': sorted(ROUTES),
            'physical_ext4_signed_lighttpd_loopback': True}


def verify(phase: str, port: str, capture: bytes, host_log: str,
           host_dir: Path, disk: Path) -> dict:
    if phase not in ('first', 'reopen'):
        raise ValueError('invalid independent native QEMU boot phase')
    sources = {route: disk_source.source_bytes(phase, host_dir, route)
               for route in ROUTES}
    if len(set(sources.values())) != len(ROUTES):
        raise ValueError('[M223 HOST FAIL] duplicate host random sources')
    result = host_log_oracle(phase, host_log, sources)
    result.update(real_http_flows(capture, sources))
    result.update(physical_downloads(port, disk, phase, sources))
    result.update({'phase': phase, 'original_signed_clients':
                   ['usr/bin/wget', 'usr/bin/aria2c', 'usr/sbin/lighttpd'],
                   'acceptance': 'real-host-random-http-get-200-body-dual-pcap-physical-ext4'})
    return result


def main() -> None:
    if len(sys.argv) != 8:
        raise SystemExit('usage: m223-http-wire-check.py first|reopen ISA PCAP HOST_LOG HOST_DIR DISK OUT.json')
    phase, port = sys.argv[1:3]
    capture, log, directory, disk, output = map(Path, sys.argv[3:])
    inputs = (capture, log, directory, disk, output)
    if len({str(path.resolve(strict=False)) for path in inputs}) != len(inputs) or \
            any(path.is_symlink() for path in inputs) or output.exists():
        raise SystemExit('[M223 HOST FAIL] stale/symlinked/overlapping evidence')
    result = verify(phase, port, capture.read_bytes(), log.read_text(), directory, disk)
    output.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n')
    print(f'[M223 REAL HTTP WIRE+DISK PASS] {phase} {port}: 2 original clients '
          'two real GET/200/full bodies host log+QEMU PCAP+physical ext4; '
          'original lighttpd loopback via signed wget')


if __name__ == '__main__':
    main()
