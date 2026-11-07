#!/usr/bin/env python3
"""UNIT ONLY: deterministic fake frames stress M228 real-PCAP oracle checks.

This is NOT hostfwd, an original Alpine boot or a physical disk acceptance.
Only scripts/m228-inbound-oracle.py on native CI artifacts may claim those.
"""

import hashlib
import importlib.util
import ipaddress
import json
from pathlib import Path
import struct
import tempfile


source = Path(__file__).with_name('m228-inbound-oracle.py')
spec = importlib.util.spec_from_file_location('m228_host_ingress_unit', source)
assert spec and spec.loader
owner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owner)


def ethernet_tcp(src: str, dst: str, sport: int, dport: int, seq: int,
                 flags: int, payload: bytes = b'') -> bytes:
    src_bytes = ipaddress.IPv4Address(src).packed
    dst_bytes = ipaddress.IPv4Address(dst).packed
    tcp = struct.pack('!HHIIHHHH', sport, dport, seq, 0,
                      (5 << 12) | flags, 65535, 0, 0) + payload
    ipv4 = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(tcp), 0,
                       0, 64, 6, 0, src_bytes, dst_bytes)
    return b'\xff' * 6 + b'\x01' * 6 + b'\x08\x00' + ipv4 + tcp


def pcap(frames: list[bytes]) -> bytes:
    capture = bytearray(struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0,
                                    0, 65535, 1))
    for frame in frames:
        capture.extend(struct.pack('<IIII', 0, 0, len(frame), len(frame)))
        capture.extend(frame)
    return bytes(capture)


def fixture(phase: str, nonce: str) -> tuple[dict, bytes]:
    path = f'/index.txt?m228_phase={phase}&nonce={nonce}'
    receipt = {
        'phase': phase,
        'transport': 'host-localhost-QEMU-user-hostfwd-guest-eth0',
        'host': '127.0.0.1',
        'host_port': 15433,
        'guest_port': 15432,
        'http_path': path,
        'http_status': 200,
        'body_sha256': hashlib.sha256(owner.BODY).hexdigest(),
        'body_length': len(owner.BODY),
        'http_server_header': 'lighttpd/1.4.82',
        'attempts': 1,
        'timestamp_utc': '2026-11-07T11:00:00Z' if phase == 'first'
                         else '2026-11-07T11:01:00Z',
    }
    remote, guest, source_port, guest_port = '10.0.2.2', '10.0.2.15', 49152, 15432
    request = (f'GET {path} HTTP/1.1\r\nHost: naseed-m228-guest\r\n'
               'Cache-Control: no-store\r\nConnection: close\r\n\r\n').encode()
    response = (b'HTTP/1.1 200 OK\r\nServer: lighttpd/1.4.82\r\n'
                b'Content-Length: 31\r\n\r\n' + owner.BODY)
    packets = [
        ethernet_tcp(remote, guest, source_port, guest_port, 1000, 0x02),
        ethernet_tcp(guest, remote, guest_port, source_port, 7000, 0x12),
        ethernet_tcp(remote, guest, source_port, guest_port, 1001, 0x10),
        ethernet_tcp(remote, guest, source_port, guest_port, 1001, 0x18, request),
        ethernet_tcp(guest, remote, guest_port, source_port, 7001, 0x18, response),
    ]
    return receipt, pcap(packets)


def reject(test) -> None:
    try:
        test()
    except ValueError:
        pass
    else:
        raise AssertionError('oracle accepted fake/contradictory witness')


def check() -> None:
    first, cap1 = fixture('first', 'a' * 40)
    reopen, cap2 = fixture('reopen', 'b' * 40)
    assert owner.physical_tcp_hostfwd(cap1, first)['actual_qemu_ethernet_inbound_connections'] == 1
    assert owner.physical_tcp_hostfwd(cap2, reopen)['actual_qemu_ethernet_inbound_connections'] == 1
    reject(lambda: owner.physical_tcp_hostfwd(cap2, first))
    reject(lambda: owner.physical_tcp_hostfwd(cap1[:-30], first))
    reject(lambda: owner.physical_tcp_hostfwd(pcap([]), first))
    # A non-SYN-ACK packet cannot impersonate QEMU's real TCP handshake.
    # Build the same capture with the first SYN removed, otherwise valid GET.
    frames = list(owner.PCAP.pcap_frames(cap1))
    reject(lambda: owner.physical_tcp_hostfwd(pcap(frames[1:]), first))
    with tempfile.TemporaryDirectory(prefix='m228-inbound-oracle-unit-') as tmp:
        base = Path(tmp)
        root = base / 'original'
        root.mkdir()
        disk = base / 'physical.img'
        disk.write_bytes(b'UNIT PLACEHOLDER, NEVER ACCEPTANCE')
        r1, r2 = base / 'first.json', base / 'reopen.json'
        p1, p2 = base / 'first.pcap', base / 'reopen.pcap'
        r1.write_text(json.dumps(first))
        r2.write_text(json.dumps(reopen))
        p1.write_bytes(cap1)
        p2.write_bytes(cap2)
        assert owner.read_receipt(r1, 'first')['phase'] == 'first'
        assert owner.read_receipt(r2, 'reopen')['phase'] == 'reopen'
        wrong = dict(first, body_sha256='0' * 64)
        r1.write_text(json.dumps(wrong))
        reject(lambda: owner.read_receipt(r1, 'first'))
        r1.write_text(json.dumps(first))
        # UNIT stub only: test orchestration of receipts/captures, never disk.
        original_verify = owner.M227.verify
        calls = []
        owner.M227.verify = lambda *args, **kwargs: calls.append((args, kwargs))
        try:
            evidence = owner.verify('amd64', root, disk, r1, r2, p1, p2)
            assert evidence['signed_original_lighttpd_openrc_ext4'] is True
            assert calls[0][1] == {'expected_bind': b'0.0.0.0'}
            r2.write_text(json.dumps(dict(reopen, http_path=first['http_path'])))
            reject(lambda: owner.verify('amd64', root, disk, r1, r2, p1, p2))
            r2.write_text(json.dumps(reopen))
            p2.write_bytes(cap1)
            reject(lambda: owner.verify('amd64', root, disk, r1, r2, p1, p2))
        finally:
            owner.M227.verify = original_verify
    print('[M228 INBOUND ORACLE UNIT PASS] nonce/phase strict host receipts, '
          'two independent Ethernet handshakes GET/HTTP full body and ext4 '
          'delegation; NOT native QEMU acceptance')


if __name__ == '__main__':
    check()
