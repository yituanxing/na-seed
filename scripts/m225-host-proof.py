#!/usr/bin/env python3
"""Fail-closed M225 host FTP/REST, Ethernet PCAP, and ext4 integration oracle.

Nothing here treats locally generated PCAP or host-only socket smoke as a
native Alpine PASS.  Only verify() on an actual QEMU capture and extracted
physical guest disk may produce an acceptance record.
"""

import hashlib
from importlib.util import module_from_spec, spec_from_file_location
import ipaddress
import json
from pathlib import Path
import re
import struct
import sys
import tempfile


def load(name, alias):
    spec = spec_from_file_location(alias, Path(__file__).with_name(name))
    if spec is None or spec.loader is None:
        raise RuntimeError('[M225 FAIL] required original network/disk oracle missing')
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


host = load('m225-host-services.py', 'm225_host_sources')
pcap = load('m196-public-wire-check.py', 'm225_actual_pcap')
m224 = load('m224-tcp-wire.py', 'm225_tcp_reassembly')
m191 = load('m191-alpine-root-disk.py', 'm225_physical_disk')
GATEWAY = ipaddress.IPv4Address('10.0.2.2')
GUEST_NET = ipaddress.IPv4Network('10.0.2.0/24')


def sources(phase: str, directory: Path) -> dict:
    result = {item: host.source(phase, directory, item) for item in host.ITEMS}
    if len(set(result.values())) != len(host.ITEMS):
        raise ValueError('[M225 HOST FAIL] duplicate cross-application random source')
    return result


def two_phase_sources(first: Path, reopen: Path) -> dict:
    """Must be called by the two-boot coordinator, not inferred from markers."""
    left, right = sources('first', first), sources('reopen', reopen)
    for item in host.ITEMS:
        if left[item] == right[item]:
            raise ValueError('[M225 HOST FAIL] first/reopen reused host random payload')
    return {item: {'first': host.digest(left[item]), 'reopen': host.digest(right[item])}
            for item in host.ITEMS}


def receipts(phase: str, directory: Path, log: str, *, ftp_port=host.FTP_PORT,
             rest_port=host.REST_PORT, ftp_only=False) -> dict:
    expected = sources(phase, directory)
    if log.splitlines().count(f'[M225 HOST READY] phase={phase} ftp={ftp_port} rest={rest_port}') != 1 or \
            '[M225 HOST DENY]' in log:
        raise ValueError('[M225 HOST FAIL] no unique host READY or rejected transfer')
    passive = []
    observed = []
    pattern = re.compile(r'M225_HOST_(FTP|REST) phase=(first|reopen) '
                         r'op=(RETR|STOR|GET|PUT) item=(lftp|rclone|restic) '
                         r'bytes=([0-9]+) sha256=([0-9a-f]{64})\Z')
    for line in log.splitlines():
        if line.startswith('M225_HOST_FTP_PASSIVE '):
            match = re.fullmatch(r'M225_HOST_FTP_PASSIVE phase=(first|reopen) port=([0-9]+)', line)
            if not match or match.group(1) != phase or not 1024 <= int(match.group(2)) <= 65535:
                raise ValueError('[M225 HOST FAIL] foreign/invalid passive port')
            passive.append(int(match.group(2)))
        if line.startswith('M225_HOST_FTP phase=') or line.startswith('M225_HOST_REST phase='):
            match = pattern.fullmatch(line)
            if match is None or match.group(2) != phase:
                raise ValueError('[M225 HOST FAIL] forged/malformed actual host receipt')
            protocol, _, op, item, size, sha = match.groups()
            if ((protocol == 'FTP') != (item in host.FTP_ITEMS) or
                    (op not in ('RETR', 'STOR') if protocol == 'FTP' else op not in ('GET', 'PUT')) or
                    int(size) != len(expected[item]) or sha != host.digest(expected[item])):
                raise ValueError('[M225 HOST FAIL] transfer bytes/source hash mismatch')
            observed.append((protocol, op, item))
    needed = {('FTP', action, item) for item in host.FTP_ITEMS for action in ('RETR', 'STOR')}
    if not ftp_only:
        needed.update({('REST', action, 'restic') for action in ('GET', 'PUT')})
    if set(observed) != needed or len(observed) != len(needed) or len(passive) < 4:
        raise ValueError('[M225 HOST FAIL] missing/duplicate real FTP/REST request')
    for item in (host.FTP_ITEMS if ftp_only else host.ITEMS):
        expected_bytes = expected[item]
        upload = directory / (item + '.upload')
        if upload.is_symlink() or upload.read_bytes() != expected_bytes:
            raise ValueError('[M225 HOST FAIL] actual received upload differs from host source')
    return {'host_real_receipts': len(observed), 'ftp_passive_ports': passive,
            'host_random_sha256': {name: host.digest(body) for name, body in expected.items()}}


def flows(capture: bytes, ports: set[int]) -> list[dict]:
    """Independent QEMU slirp TCP flows; no UART/host log substitutes."""
    found = {}
    for index, frame in enumerate(pcap.pcap_frames(capture)):
        packet = pcap.ipv4_packet(frame)
        if packet is None:
            continue
        src, dst, protocol, data = packet
        if protocol != 6 or len(data) < 20:
            continue
        sport, dport, seq = struct.unpack_from('!HHI', data)
        offset = (data[12] >> 4) * 4
        if offset < 20 or offset > len(data):
            continue
        outgoing = src in GUEST_NET and src != GATEWAY and dst == GATEWAY and dport in ports
        incoming = dst in GUEST_NET and dst != GATEWAY and src == GATEWAY and sport in ports
        if not (outgoing or incoming):
            continue
        server_port, guest_port = (dport, sport) if outgoing else (sport, dport)
        if guest_port < 1024:
            continue
        key = (str(src if outgoing else dst), guest_port, server_port)
        flow = found.setdefault(key, {'port': server_port, 'syn': [], 'synack': [],
                                      'ack': [], 'client': [], 'server': []})
        flags = data[13]
        if outgoing:
            if flags & 0x02 and not flags & 0x10:
                flow['syn'].append(index)
            if flags & 0x10 and not flags & 0x02:
                flow['ack'].append(index)
            if len(data) > offset:
                flow['client'].append((seq, data[offset:]))
        else:
            if flags & 0x12 == 0x12:
                flow['synack'].append(index)
            if len(data) > offset:
                flow['server'].append((seq, data[offset:]))
    result = []
    for flow in found.values():
        if not (flow['syn'] and flow['synack'] and flow['ack'] and
                min(flow['syn']) < min(flow['synack']) < min(flow['ack'])):
            continue
        flow['client'] = m224.contiguous_payload(flow['client'])
        flow['server'] = m224.contiguous_payload(flow['server'])
        result.append(flow)
    return result


def real_wire(capture: bytes, phase: str, directory: Path,
              passive_ports: list[int], *, ftp_only=False) -> dict:
    expected = sources(phase, directory)
    if len(passive_ports) < 4:
        raise ValueError('[M225 WIRE FAIL] four actual FTP passive transactions missing')
    live = flows(capture, {host.FTP_PORT, host.REST_PORT, *passive_ports})
    control_flows = [flow for flow in live if flow['port'] == host.FTP_PORT]
    control = b'\r\n'.join(flow['client'] for flow in control_flows)
    replies = b'\r\n'.join(flow['server'] for flow in control_flows)
    # The chosen server-side data ports must be advertised in *actual guest
    # Ethernet control replies* (EPSV/229 or gateway-safe PASV/227), not just
    # untrusted host log text. This catches the classic slirp 127.0.0.1 PASV
    # mismatch and forged host-only ports that never reached the guest.
    advertised = {int(raw) for raw in re.findall(
        rb'229 [^\r\n]*\(\|\|\|([0-9]{2,5})\|\)', replies)}
    advertised.update(256 * int(hi) + int(lo) for hi, lo in re.findall(
        rb'227 [^\r\n]*\(10,0,2,2,([0-9]{1,3}),([0-9]{1,3})\)', replies))
    if not advertised.intersection(passive_ports):
        raise ValueError('[M225 WIRE FAIL] no real FTP EPSV/PASV dynamic data port advertised')
    for item in host.FTP_ITEMS:
        if not all(re.search(rb'(?m)(?:^|\r\n)' + op.encode() + rb' /?' +
                             item.encode() + rb'\.' + suffix.encode() + rb'\r\n',
                             control) for op, suffix in (('RETR', 'source'),
                                                         ('STOR', 'upload'))):
            raise ValueError('[M225 WIRE FAIL] actual FTP control commands absent')
        if not any(flow['port'] in passive_ports and flow['port'] in advertised and
                   flow['server'] == expected[item]
                   for flow in live):
            raise ValueError('[M225 WIRE FAIL] actual FTP source not delivered to guest')
        if not any(flow['port'] in passive_ports and flow['port'] in advertised and
                   flow['client'] == expected[item]
                   for flow in live):
            raise ValueError('[M225 WIRE FAIL] actual FTP upload bytes absent')
    if not ftp_only:
        rest = [flow for flow in live if flow['port'] == host.REST_PORT]
        if not any(b'GET /restic.source HTTP/1.' in flow['client'] and
                   b'HTTP/1.1 200 ' in flow['server'] and
                   flow['server'].split(b'\r\n\r\n', 1)[-1] == expected['restic']
                   for flow in rest):
            raise ValueError('[M225 WIRE FAIL] real REST source GET/200/full body missing')
        if not any(b'PUT /restic.upload HTTP/1.' in flow['client'] and
                   flow['client'].split(b'\r\n\r\n', 1)[-1] == expected['restic'] and
                   b'HTTP/1.1 201 ' in flow['server']
                   for flow in rest):
            raise ValueError('[M225 WIRE FAIL] real REST upload PUT/201/full body missing')
    result = {'pcap_sha256': hashlib.sha256(capture).hexdigest(),
              'real_established_tcp_sessions': len(live),
              'ftp_real_bidirectional_sources': list(host.FTP_ITEMS)}
    if not ftp_only:
        result['rest_real_bidirectional_source'] = True
    return result


def physical_files(phase: str, directory: Path, read_physical, *, ftp_only=False) -> dict:
    expected = sources(phase, directory)
    for item in (host.FTP_ITEMS if ftp_only else host.ITEMS):
        body = expected[item]
        for filename in ('download.bin', 'uploaded.bin'):
            path = f'/root/m225-work/{item}.{phase}/{filename}'
            if read_physical(path) != body:
                raise ValueError(f'[M225 DISK FAIL] real native {item} {filename} '
                                 'does not match independent host-only random bytes')
    return {'physical_ext4_exact_challenges': sorted(host.FTP_ITEMS if ftp_only else expected)}


def verify(phase: str, port: str, capture: Path, log: Path,
           directory: Path, disk: Path) -> dict:
    if phase not in host.PHASES or port not in m191.ARCH_MACHINE:
        raise ValueError('[M225 FAIL] invalid native QEMU phase or ISA')
    # M225 accepts only original lftp+rclone FTP: the bounded HTTP REST
    # fixture is NOT a restic repository protocol and carries no PASS.
    inspected = receipts(phase, directory, log.read_text(), ftp_only=True)
    inspected.update(real_wire(capture.read_bytes(), phase, directory,
                               inspected['ftp_passive_ports'], ftp_only=True))
    with tempfile.TemporaryDirectory(prefix='m225-real-ext4-') as tmp:
        partition = Path(tmp) / 'root.ext4'
        m191.extract_partition(disk.resolve(strict=True), partition)
        inspected.update(physical_files(phase, directory,
                                        lambda path: m191.debugfs_cat(partition, path),
                                        ftp_only=True))
    inspected.update({'phase': phase, 'isa': port, 'acceptance':
                      'original-lftp-rclone-ftp-duplex-tcp-pcap-physical-ext4'})
    return inspected


if __name__ == '__main__':
    if len(sys.argv) != 8:
        raise SystemExit('usage: m225-host-proof.py first|reopen ISA PCAP HOST_LOG HOST_DIR DISK OUT.json')
    boot_phase, isa = sys.argv[1:3]
    capture_path, log_path, host_dir, disk_path, destination = map(Path, sys.argv[3:])
    inputs = (capture_path, log_path, host_dir, disk_path, destination)
    if (any(path.is_symlink() for path in inputs) or
            len(set(str(path.resolve(strict=False)) for path in inputs)) != len(inputs) or
            destination.exists()):
        raise SystemExit('[M225 FAIL] reused/symlinked/overlapping physical evidence')
    result = verify(boot_phase, isa, capture_path, log_path, host_dir, disk_path)
    destination.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n')
    print(f'[M225 ORIGINAL REMOTE STORAGE WIRE+DISK PASS] {boot_phase} {isa}')
