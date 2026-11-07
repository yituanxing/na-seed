#!/usr/bin/env python3
"""Host-only *physical* M228 hostfwd inbound HTTP oracle, both real QEMUs.

Two independently nonce-challenged host GET receipts must agree with TWO
different QEMU Ethernet PCAP captures and the final original signed Alpine
lighttpd/OpenRC physical ext4 disk. A UART marker, client JSON, or loopback
GET alone cannot pass this oracle. No simulated host HTTP server is accepted.
"""

import hashlib
import importlib.util
import ipaddress
import json
from pathlib import Path
import re
import struct
import sys


def load(name: str, alias: str):
    source = Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location(alias, source)
    if spec is None or spec.loader is None:
        raise RuntimeError('M228 missing original verified helper: ' + name)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


M227 = load('m227-alpine-disk-check.py', 'm228_original_signed_ext4')
M223 = load('m223-http-wire-check.py', 'm228_original_tcp_pcap')
PCAP = M223.pcap
BODY = M227.DOCUMENT
BODY_SHA256 = hashlib.sha256(BODY).hexdigest()
HOST = ipaddress.IPv4Address('10.0.2.2')
GUEST_NET = ipaddress.IPv4Network('10.0.2.0/24')
GUEST_PORT = 15432
PHASES = ('first', 'reopen')
PATH = re.compile(r'/index\.txt\?m228_phase=(first|reopen)&nonce=([0-9a-f]{40})\Z')


def fail(message: str) -> None:
    raise ValueError('[M228 HOST INBOUND FAIL] ' + message)


def read_receipt(path: Path, phase: str) -> dict:
    if path.is_symlink() or not path.is_file():
        fail('missing or symlinked ' + phase + ' independent host TCP receipt')
    try:
        receipt = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        fail('malformed ' + phase + ' host TCP receipt: ' + str(exc))
    if not isinstance(receipt, dict):
        fail('non-object ' + phase + ' host TCP receipt')
    fixed = {
        'phase': phase,
        'transport': 'host-localhost-QEMU-user-hostfwd-guest-eth0',
        'host': '127.0.0.1',
        'guest_port': GUEST_PORT,
        'http_status': 200,
        'body_sha256': BODY_SHA256,
        'body_length': len(BODY),
    }
    for key, expected in fixed.items():
        if type(receipt.get(key)) is not type(expected) or receipt.get(key) != expected:
            fail(f'{phase} host receipt {key} is not exact original guest HTTP data')
    host_port = receipt.get('host_port')
    if type(host_port) is not int or not 1 <= host_port <= 65535:
        fail(phase + ' invalid forwarded TCP host port')
    attempts = receipt.get('attempts')
    if type(attempts) is not int or attempts < 1:
        fail(phase + ' did not record a real host HTTP attempt')
    route = receipt.get('http_path')
    if not isinstance(route, str):
        fail(phase + ' missing nonce-challenged host GET path')
    match = PATH.fullmatch(route)
    if match is None or match.group(1) != phase:
        fail(phase + ' host HTTP request lacks unique nonce and correct phase')
    if not isinstance(receipt.get('timestamp_utc'), str) or not re.fullmatch(
            r'20[0-9]{2}-[01][0-9]-[0-3][0-9]T[0-2][0-9]:[0-5][0-9]:[0-5][0-9]Z',
            receipt['timestamp_utc']):
        fail(phase + ' missing UTC host response time')
    server = receipt.get('http_server_header')
    if not isinstance(server, str) or (server and 'lighttpd' not in server.lower()):
        fail(phase + ' response identifies a different HTTP server')
    return receipt


def tcp_segment(data: bytes):
    if len(data) < 20:
        return None
    sport, dport, seq = struct.unpack_from('!HHI', data)
    header_length = (data[12] >> 4) * 4
    if header_length < 20 or header_length > len(data):
        return None
    return sport, dport, seq, data[13], data[header_length:]


def physical_tcp_hostfwd(pcap_bytes: bytes, receipt: dict) -> dict:
    """Require host-SYN -> guest-SYNACK -> host-ACK -> GET -> HTTP 200/body.

    QEMU slirp maps host localhost into the real virtual eth0 link as peer
    10.0.2.2. The guest's own 127.0.0.1 loopback cannot produce these NIC
    frames. Reassembly reuses the proven M223 gap/retransmission checker.
    """
    phase = receipt['phase']
    requested = (f'GET {receipt["http_path"]} HTTP/1.1\r\n').encode('ascii')
    flows: dict[tuple[str, int], dict] = {}
    for index, frame in enumerate(PCAP.pcap_frames(pcap_bytes)):
        packet = PCAP.ipv4_packet(frame)
        if packet is None:
            continue
        src, dst, protocol, ip_payload = packet
        if protocol != 6:
            continue
        segment = tcp_segment(ip_payload)
        if segment is None:
            continue
        sport, dport, sequence, flags, payload = segment
        to_guest = (src == HOST and dst in GUEST_NET and dst != HOST and
                    dport == GUEST_PORT and sport >= 1024)
        to_host = (src in GUEST_NET and src != HOST and dst == HOST and
                   sport == GUEST_PORT and dport >= 1024)
        if not (to_host or to_guest):
            continue
        guest_ip = str(dst if to_guest else src)
        remote_port = sport if to_guest else dport
        flow = flows.setdefault((guest_ip, remote_port), {
            'syn': [], 'synack': [], 'ack': [], 'request': [], 'response': [],
        })
        if to_guest:
            if flags & 0x02 and not flags & 0x10:
                flow['syn'].append(index)
            if flags & 0x10 and not flags & 0x02:
                flow['ack'].append(index)
            if payload:
                flow['request'].append((sequence, payload))
        else:
            if flags & 0x12 == 0x12:
                flow['synack'].append(index)
            if payload:
                flow['response'].append((sequence, payload))
    matches = []
    for (guest_ip, remote_port), flow in flows.items():
        if not (flow['syn'] and flow['synack'] and flow['ack'] and
                min(flow['syn']) < min(flow['synack']) < min(flow['ack'])):
            continue
        try:
            request = M223.contiguous_payload(flow['request'])
            response = M223.contiguous_payload(flow['response'])
        except ValueError as exc:
            fail(phase + ' contradictory TCP retransmission: ' + str(exc))
        if not request.startswith(requested):
            continue
        if (b'\r\nHost: naseed-m228-guest\r\n' not in request or
                b'\r\n\r\n' not in request):
            continue
        if not response.startswith((b'HTTP/1.1 200 ', b'HTTP/1.0 200 ')):
            continue
        if b'\r\n\r\n' not in response or response.split(b'\r\n\r\n', 1)[1] != BODY:
            continue
        matches.append({'guest_ip': guest_ip, 'remote_tcp_port': remote_port,
                        'http_bytes': len(BODY)})
    if not matches:
        fail(phase + ' physical Ethernet PCAP lacks nonce-matched '
             'host SYN/SYNACK/ACK + original GET + lighttpd HTTP 200/full body')
    return {
        'phase': phase,
        'actual_qemu_ethernet_inbound_connections': len(matches),
        'guest_ips': sorted({row['guest_ip'] for row in matches}),
        'http_full_body_bytes': len(BODY),
        'host_challenge_path': receipt['http_path'],
    }


def verify(port: str, root: Path, disk: Path, receipt_first: Path,
           receipt_reopen: Path, pcap_first: Path, pcap_reopen: Path) -> dict:
    evidence = (root, disk, receipt_first, receipt_reopen, pcap_first, pcap_reopen)
    if len({str(path.resolve(strict=False)) for path in evidence}) != len(evidence):
        fail('overlapping signed root, disk, receipts or phase captures')
    if any(path.is_symlink() for path in evidence):
        fail('symlinked source/disk/receipt/PCAP evidence')
    receipts = {
        'first': read_receipt(receipt_first, 'first'),
        'reopen': read_receipt(receipt_reopen, 'reopen'),
    }
    if receipts['first']['host_port'] != receipts['reopen']['host_port']:
        fail('hostfwd port drift across independent native QEMUs')
    if receipts['first']['http_path'] == receipts['reopen']['http_path']:
        fail('repeated host nonce between first and second original PID1')
    if receipts['first']['timestamp_utc'] >= receipts['reopen']['timestamp_utc']:
        fail('second original QEMU HTTP response did not follow first')
    captures = {}
    for phase, path in (('first', pcap_first), ('reopen', pcap_reopen)):
        if not path.is_file():
            fail(phase + ' native QEMU Ethernet PCAP absent')
        raw = path.read_bytes()
        if not raw:
            fail(phase + ' native QEMU Ethernet PCAP empty')
        captures[phase] = raw
    if hashlib.sha256(captures['first']).digest() == hashlib.sha256(captures['reopen']).digest():
        fail('two native QEMU Ethernet captures are identical')
    physical = {phase: physical_tcp_hostfwd(captures[phase], receipts[phase])
                for phase in PHASES}
    for phase in PHASES:
        physical[phase]['pcap_sha256'] = hashlib.sha256(captures[phase]).hexdigest()
    # M227's real signed package DB + original OpenRC script, networking boot,
    # lighttpd default, SQLite/Git persistence, strict fsck and physical disk.
    # Only M228 opts into the explicit wildcard NIC bind; M227 remains local.
    M227.verify(port, root, disk, expected_bind=b'0.0.0.0')
    return {
        'acceptance': 'original-Alpine-two-PID1-hostfwd-real-ETH-TCP-GET-full-HTTP-ext4',
        'port': port,
        'both_real_host_responses': {
            phase: {'status': receipts[phase]['http_status'],
                    'body_sha256': receipts[phase]['body_sha256'],
                    'fresh_nonce_path': receipts[phase]['http_path'],
                    'response_timestamp_utc': receipts[phase]['timestamp_utc']}
            for phase in PHASES
        },
        'both_independent_physical_eth_pcap': physical,
        'signed_original_lighttpd_openrc_ext4': True,
    }


def main() -> None:
    if len(sys.argv) != 9:
        raise SystemExit('usage: m228-inbound-oracle.py ISA SIGNED_ROOT DISK '
                         'FIRST_RECEIPT REOPEN_RECEIPT FIRST_PCAP REOPEN_PCAP OUT.json')
    port = sys.argv[1]
    root, disk, first, reopen, pcap1, pcap2, output = map(Path, sys.argv[2:])
    if output.exists() or output.is_symlink() or any(
            path.resolve(strict=False) == output.resolve(strict=False)
            for path in (root, disk, first, reopen, pcap1, pcap2)):
        fail('stale, symlinked or overlapping output receipt')
    result = verify(port, root, disk, first, reopen, pcap1, pcap2)
    output.write_text(json.dumps(result, sort_keys=True, indent=2) + '\n', encoding='utf-8')
    print(f'[M228 ORIGINAL ALPINE HOST INGRESS PASS] {port}: two native PID1, '
          'two real hostfwd host GET/200/full bodies, independent QEMU NIC '
          'handshakes+PCAPs, original lighttpd/OpenRC, strict physical ext4')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, RuntimeError) as exc:
        raise SystemExit('[M228 INBOUND ORACLE FAIL] ' + str(exc)) from exc
