#!/usr/bin/env python3
"""UNIT ONLY fake TCP/PCAP fixtures; never an original native QEMU PASS."""

import hashlib
import importlib.util
import ipaddress
import json
from pathlib import Path
import socketserver
import struct
import tempfile
from threading import Thread
import time


def load(filename: str, name: str):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    assert spec and spec.loader
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


host = load('m229-host-resp-check.py', 'm229_host_unit')
oracle = load('m229-resp-oracle.py', 'm229_wire_unit')
TOKEN = 'f' * 64


def reject(work):
    try:
        work()
    except (ValueError, TimeoutError):
        pass
    else:
        raise AssertionError('M229 fake/unsafe data unexpectedly accepted')


def ethernet(src: str, dst: str, sport: int, dport: int,
             seq: int, flags: int, content: bytes = b'') -> bytes:
    ips = [ipaddress.IPv4Address(x).packed for x in (src, dst)]
    tcp = struct.pack('!HHIIHHHH', sport, dport, seq, 0,
                      (5 << 12) | flags, 65535, 0, 0) + content
    ipv4 = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(tcp), 0,
                       0, 64, 6, 0, *ips)
    return b'\xff' * 6 + b'\x01' * 6 + b'\x08\x00' + ipv4 + tcp


def capture(frames: list[bytes]) -> bytes:
    packet = bytearray(struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0,
                                   0, 65535, 1))
    for frame in frames:
        packet.extend(struct.pack('<IIII', 0, 0, len(frame), len(frame)))
        packet.extend(frame)
    return bytes(packet)


def fixture(phase: str, nonce: str, key: str, value: str) -> tuple[dict, bytes]:
    rows = [('AUTH', TOKEN), ('PING',), ('ECHO', nonce)]
    if phase == 'first':
        rows += [('SET', key, value), ('GET', key), ('SAVE',)]
    else:
        rows.append(('GET', key))
    request = b''.join(host.command(*args) for args in rows)
    temporary = {'phase': phase, 'nonce': nonce, 'key': key, 'value': value}
    response = oracle.expected_reply(temporary)
    receipt = dict(temporary, transport=host.TRANSPORT, host='127.0.0.1',
                   host_port=15445, guest_port=15445, auth_required=True,
                   commands=[r[0] for r in rows],
                   value_sha256=hashlib.sha256(value.encode()).hexdigest(),
                   request_sha256=hashlib.sha256(request).hexdigest(),
                   response_sha256=hashlib.sha256(response).hexdigest(),
                   attempts=1,
                   timestamp_utc=('2026-11-07T13:00:00Z' if phase == 'first'
                                  else '2026-11-07T13:02:00Z'))
    ip_host, ip_guest, client_port = '10.0.2.2', '10.0.2.15', 49152
    frames = [
        ethernet(ip_host, ip_guest, client_port, host.GUEST_PORT, 1000, 0x02),
        ethernet(ip_guest, ip_host, host.GUEST_PORT, client_port, 9000, 0x12),
        ethernet(ip_host, ip_guest, client_port, host.GUEST_PORT, 1001, 0x10),
        ethernet(ip_host, ip_guest, client_port, host.GUEST_PORT, 1001, 0x18, request),
        ethernet(ip_guest, ip_host, host.GUEST_PORT, client_port, 9001, 0x18, response),
    ]
    return receipt, capture(frames)


def check_host_socket() -> None:
    memory = {}

    class Handler(socketserver.StreamRequestHandler):
        def handle(self):
            while True:
                head = self.rfile.readline()
                if not head:
                    return
                assert head.startswith(b'*') and head.endswith(b'\r\n')
                fields = []
                for _ in range(int(head[1:-2])):
                    size = self.rfile.readline()
                    assert size.startswith(b'$') and size.endswith(b'\r\n')
                    data = self.rfile.read(int(size[1:-2]) + 2)
                    assert data.endswith(b'\r\n')
                    fields.append(data[:-2].decode())
                operation = fields[0]
                if operation == 'AUTH':
                    assert fields[1] == TOKEN
                    answer = b'+OK\r\n'
                elif operation == 'PING':
                    answer = b'+PONG\r\n'
                elif operation == 'ECHO':
                    value = fields[1]
                    answer = f'${len(value)}\r\n{value}\r\n'.encode()
                elif operation == 'SET':
                    memory[fields[1]] = fields[2]
                    answer = b'+OK\r\n'
                elif operation == 'GET':
                    value = memory.get(fields[1], '')
                    answer = f'${len(value)}\r\n{value}\r\n'.encode()
                elif operation == 'SAVE':
                    answer = b'+OK\r\n'
                else:
                    raise AssertionError('unexpected mocked Valkey command')
                self.wfile.write(answer)

    class Server(socketserver.ThreadingTCPServer):
        allow_reuse_address = True
        daemon_threads = True

    with Server(('127.0.0.1', 0), Handler) as server:
        worker = Thread(target=server.serve_forever, kwargs={'poll_interval': 0.02})
        worker.start()
        try:
            key, value = 'm229:' + 'a' * 48, 'c' * 64
            first = host.transaction(server.server_address[1], 'first', TOKEN,
                                     key, value, 'a' * 48, 3)
            second = host.transaction(server.server_address[1], 'reopen', TOKEN,
                                      key, value, 'b' * 48, 3)
            assert first['commands'] == ['AUTH', 'PING', 'ECHO', 'SET', 'GET', 'SAVE']
            assert second['commands'] == ['AUTH', 'PING', 'ECHO', 'GET']
            assert memory[key] == value
            assert TOKEN not in json.dumps(first) + json.dumps(second)
        finally:
            server.shutdown()
            worker.join()


def check() -> None:
    for invalid in ('', '0', '-1', 'x', '65536', ' 80', '80\n', '１２３'):
        reject(lambda s=invalid: host.valid_port(s))
    assert host.valid_port('00015445') == 15445
    assert host.valid_port('65535') == 65535
    with tempfile.TemporaryDirectory(prefix='m229-host-unit-') as tmp:
        base = Path(tmp)
        uart = base / 'uart'
        marker = host.READY['first']

        def append():
            time.sleep(.1)
            uart.write_bytes(b'echo ' + marker + b' NOT A GUEST READY\r\n' + marker[:10])
            time.sleep(.1)
            with uart.open('ab') as stream:
                stream.write(marker[10:] + b'\r\n')

        writer = Thread(target=append)
        writer.start()
        host.wait_for_guest_ready(uart, marker, 3)
        writer.join()
        key, value = 'm229:' + 'a' * 48, 'c' * 64
        a, cap1 = fixture('first', 'a' * 48, key, value)
        b, cap2 = fixture('reopen', 'b' * 48, key, value)
        assert oracle.physical_valkey_flow(cap1, a)['valkey_tcp_verified']
        assert oracle.physical_valkey_flow(cap2, b)['valkey_tcp_verified']
        reject(lambda: oracle.physical_valkey_flow(cap2, a))
        wrong_frames = list(oracle.PCAP.pcap_frames(cap1))
        reject(lambda: oracle.physical_valkey_flow(capture(wrong_frames[1:]), a))
        reject(lambda: oracle.physical_valkey_flow(cap1[:-20], a))
        reject(lambda: oracle.physical_valkey_flow(capture([]), a))
        receipt1, receipt2 = base / 'first.json', base / 'reopen.json'
        pcap1, pcap2 = base / 'first.pcap', base / 'reopen.pcap'
        receipt1.write_text(json.dumps(a))
        receipt2.write_text(json.dumps(b))
        pcap1.write_bytes(cap1)
        pcap2.write_bytes(cap2)
        assert host.read_first_receipt(receipt1) == ('a' * 48, key, value)
        result = oracle.verify_transport(receipt1, receipt2, pcap1, pcap2)
        assert result['transport_only_not_ext4_acceptance'] is True
        wrong = dict(b, nonce=a['nonce'])
        receipt2.write_text(json.dumps(wrong))
        reject(lambda: oracle.verify_transport(receipt1, receipt2, pcap1, pcap2))
        receipt2.write_text(json.dumps(b))
        pcap2.write_bytes(cap1)
        reject(lambda: oracle.verify_transport(receipt1, receipt2, pcap1, pcap2))
        pcap2.write_bytes(cap2)
        receipt1.write_text(json.dumps(dict(a, request_sha256='0' * 64)))
        reject(lambda: oracle.verify_transport(receipt1, receipt2, pcap1, pcap2))
    check_host_socket()
    print('[M229 HOST RESP UNIT PASS] bounded authenticated RESP, fresh native-boot '
          'nonce/SET/GET/SAVE receipts and strict dual physical Ethernet TCP; '
          'UNIT FIXTURES ONLY, not QEMU acceptance')


if __name__ == '__main__':
    check()
