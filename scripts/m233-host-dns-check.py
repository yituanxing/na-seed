#!/usr/bin/env python3
"""Independent host DNS UDP+TCP queries into original Alpine dnsmasq.

Only this HOST generates random transaction IDs and fresh names; the guest
does not learn them via UART. A pass also requires two raw NIC PCAPs and ext4.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import secrets
import socket
import struct
import time


GUEST_PORT = 15453
RECORD = socket.inet_aton('198.51.100.233')
HEX24 = re.compile(r'[0-9a-f]{24}\Z')
HEX4 = re.compile(r'[0-9a-f]{4}\Z')


def dns_name(name: str) -> bytes:
    result = bytearray()
    for label in name.split('.'):
        encoded = label.encode('ascii')
        if not 1 <= len(encoded) <= 63:
            raise ValueError('invalid DNS label')
        result.append(len(encoded))
        result.extend(encoded)
    return bytes(result) + b'\0'


def query(nonce: str, txid: int) -> bytes:
    if HEX24.fullmatch(nonce) is None or not 0 <= txid <= 65535:
        raise ValueError('invalid host challenge')
    # RD=0: dnsmasq is only an authoritative local domain fixture.
    return (struct.pack('!HHHHHH', txid, 0, 1, 0, 0, 0) +
            dns_name(nonce + '.m233.naseed.test') + struct.pack('!HH', 1, 1))


def validate_response(request: bytes, response: bytes) -> None:
    if len(request) < 16 or len(response) < 28:
        raise ValueError('short DNS question/reply')
    qid, qflags, questions, _, _, _ = struct.unpack_from('!6H', request)
    rid, flags, qd, answers, _, _ = struct.unpack_from('!6H', response)
    if (qid != rid or qflags != 0 or questions != 1 or not flags & 0x8000 or
            flags & 0xf != 0 or qd != 1 or answers < 1 or flags & 0x0200):
        raise ValueError('DNS reply header / transaction ID mismatch')
    suffix = request[12:]
    if response[12:12 + len(suffix)] != suffix:
        raise ValueError('DNS response does not echo host random question')
    pos = 12 + len(suffix)
    matched = False
    for _ in range(answers):
        if pos + 2 > len(response):
            raise ValueError('truncated DNS answer owner')
        if response[pos] & 0xc0 == 0xc0:
            if (response[pos] & 0x3f) << 8 | response[pos + 1] != 12:
                raise ValueError('DNS answer owner does not point to question')
            pos += 2
        else:
            answer_name = dns_name_from_wire(response, pos)
            if answer_name[0] != suffix[:-4]:
                raise ValueError('incorrect DNS answer owner')
            pos = answer_name[1]
        if pos + 10 > len(response):
            raise ValueError('truncated DNS answer RR')
        rtype, rclass, _, length = struct.unpack_from('!HHIH', response, pos)
        pos += 10
        if pos + length > len(response):
            raise ValueError('DNS answer RDATA overrun')
        if rtype == 1 and rclass == 1 and length == 4 and response[pos:pos + 4] == RECORD:
            matched = True
        pos += length
    if not matched:
        raise ValueError('signed dnsmasq did not return expected authoritative A record')


def dns_name_from_wire(wire: bytes, pos: int) -> tuple[bytes, int]:
    start = pos
    while pos < len(wire):
        size = wire[pos]
        if size & 0xc0 or size > 63 or pos + 1 + size > len(wire):
            raise ValueError('bad uncompressed DNS owner')
        pos += 1 + size
        if size == 0:
            return wire[start:pos], pos
    raise ValueError('unterminated DNS owner')


def read_exact(sock: socket.socket, count: int) -> bytes:
    data = bytearray()
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise ValueError('DNS TCP EOF before reply')
        data.extend(chunk)
    return bytes(data)


def exchange(host_port: int, transport: str, request: bytes, timeout: float) -> bytes:
    if transport == 'udp':
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as conn:
            conn.settimeout(timeout)
            conn.connect(('127.0.0.1', host_port))
            conn.sendall(request)
            response = conn.recv(2048)
    elif transport == 'tcp':
        with socket.create_connection(('127.0.0.1', host_port), timeout=timeout) as conn:
            conn.settimeout(timeout)
            conn.sendall(struct.pack('!H', len(request)) + request)
            size = struct.unpack('!H', read_exact(conn, 2))[0]
            if not 12 <= size <= 2048:
                raise ValueError('DNS TCP frame length invalid')
            response = read_exact(conn, size)
    else:
        raise ValueError('unsupported DNS transport')
    validate_response(request, response)
    return response


def wait_ready(serial: Path, phase: str, timeout: int) -> None:
    end = time.monotonic() + timeout
    marker = 'M233_' + phase.upper() + '_HOST_DNS_READY'
    while time.monotonic() < end:
        if serial.is_file() and marker in serial.read_text(errors='replace'):
            return
        time.sleep(.5)
    raise TimeoutError('original Alpine UART DNS readiness not observed: ' + marker)


def run(phase: str, serial: Path, host_port: int, udp_port: int,
        receipt: Path, *, ready_timeout: int, request_timeout: int) -> dict:
    if phase not in ('first', 'reopen') or not (1 <= host_port <= 65535 and
                                              1 <= udp_port <= 65535):
        raise ValueError('invalid phase/host port')
    if receipt.exists() or receipt.is_symlink():
        raise ValueError('stale host receipt')
    wait_ready(serial, phase, ready_timeout)
    rows = {}
    identifiers = set()
    for protocol, port in (('udp', udp_port), ('tcp', host_port)):
        nonce = secrets.token_hex(12)
        txid = secrets.randbelow(65536)
        while txid in identifiers:
            txid = secrets.randbelow(65536)
        identifiers.add(txid)
        req = query(nonce, txid)
        deadline = time.monotonic() + request_timeout
        attempts = 0
        last = None
        while time.monotonic() < deadline:
            attempts += 1
            try:
                response = exchange(port, protocol, req, min(3.0, max(.2, deadline-time.monotonic())))
                break
            except (OSError, ValueError) as exc:
                last = exc
                time.sleep(.7)
        else:
            raise TimeoutError(f'actual host {protocol} DNS failed: {last}')
        rows[protocol] = {'nonce': nonce, 'transaction_id': f'{txid:04x}',
                          'attempts': attempts, 'request_hex': req.hex(),
                          'response_hex': response.hex(),
                          'response_sha256': hashlib.sha256(response).hexdigest()}
    record = {'phase': phase, 'guest_port': GUEST_PORT,
              'host': '127.0.0.1', 'tcp_host_port': host_port,
              'udp_host_port': udp_port, 'authoritative_A': '198.51.100.233',
              'utc': datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
              'dns_queries': rows}
    receipt.write_text(json.dumps(record, sort_keys=True, indent=2) + '\n')
    print(f'[M233 HOST DNS {phase.upper()} PASS] independent random UDP+TCP '
          'questions verified against original Alpine daemon via QEMU localhost hostfwd',
          flush=True)
    return record


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('phase', choices=['first', 'reopen'])
    parser.add_argument('uart', type=Path)
    parser.add_argument('tcp_host_port', type=int)
    parser.add_argument('udp_host_port', type=int)
    parser.add_argument('receipt', type=Path)
    parser.add_argument('--ready-timeout', type=int, default=2600)
    parser.add_argument('--request-timeout', type=int, default=30)
    args = parser.parse_args()
    run(args.phase, args.uart, args.tcp_host_port, args.udp_host_port,
        args.receipt, ready_timeout=args.ready_timeout,
        request_timeout=args.request_timeout)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, TimeoutError) as exc:
        raise SystemExit('[M233 HOST DNS FAIL] ' + str(exc)) from exc
