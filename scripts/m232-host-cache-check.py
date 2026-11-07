#!/usr/bin/env python3
"""Independent host memcached ASCII wire exercise across two real Alpine QEMUs.

memcached is intentionally VOLATILE. First and reopened QEMU both prove an
empty key, then independent host writes and reads a fresh random value. No
guest UART script knows the key or payload. NIC PCAP and ext4 are mandatory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import secrets
import socket
import sys
import time

GUEST_PORT = 15448
TRANSPORT = 'host-localhost-QEMU-hostfwd-guest-eth0-memcached-ascii'
READY = {'first': b'M232_FIRST_HOST_CACHE_READY',
         'reopen': b'M232_REOPEN_HOST_CACHE_READY'}
HEX48 = re.compile(r'[0-9a-f]{48}\Z')
HEX64 = re.compile(r'[0-9a-f]{64}\Z')


def wire_messages(key: str, value: str) -> tuple[bytes, bytes]:
    if HEX64.fullmatch(value) is None or not key.startswith('naseed_m232_') or \
            HEX48.fullmatch(key[12:]) is None:
        raise ValueError('malformed host-owned memcached challenge')
    requests = (f'get {key}\r\nset {key} 0 0 64\r\n{value}\r\n'
                f'get {key}\r\n').encode('ascii')
    responses = (f'END\r\nSTORED\r\nVALUE {key} 0 64\r\n{value}\r\nEND\r\n')\
        .encode('ascii')
    return requests, responses


def wait_ready(path: Path, marker: bytes, timeout: int) -> None:
    deadline, offset, tail = time.monotonic() + timeout, 0, b''
    while time.monotonic() < deadline:
        try:
            with path.open('rb') as stream:
                if stream.seek(0, 2) < offset:
                    offset, tail = 0, b''
                stream.seek(offset)
                chunk = stream.read()
                offset = stream.tell()
        except FileNotFoundError:
            chunk = b''
        if chunk:
            combined = tail + chunk
            if any(line.strip() == marker for line in
                   combined.replace(b'\r', b'').splitlines()):
                return
            tail = combined[-256:]
        time.sleep(.2)
    raise TimeoutError('original Alpine OpenRC never opened real host cache window')


def read_exact(sock: socket.socket, expected: bytes) -> None:
    data = bytearray()
    while len(data) < len(expected):
        chunk = sock.recv(min(4096, len(expected) - len(data)))
        if not chunk:
            raise ValueError('memcached TCP closed before entire response')
        data.extend(chunk)
    if bytes(data) != expected:
        raise ValueError('real host memcached response differs')


def transaction(port: int, key: str, value: str, seconds: int) -> dict:
    req, resp = wire_messages(key, value)
    commands = (
        (f'get {key}\r\n'.encode(), b'END\r\n'),
        (f'set {key} 0 0 64\r\n{value}\r\n'.encode(), b'STORED\r\n'),
        (f'get {key}\r\n'.encode(),
         f'VALUE {key} 0 64\r\n{value}\r\nEND\r\n'.encode()),
    )
    assert b''.join(x for x, _ in commands) == req
    assert b''.join(x for _, x in commands) == resp
    deadline, attempts = time.monotonic() + seconds, 0
    while time.monotonic() < deadline:
        attempts += 1
        try:
            with socket.create_connection(('127.0.0.1', port), 3) as sock:
                sock.settimeout(8)
                for command, answer in commands:
                    sock.sendall(command)
                    read_exact(sock, answer)
            return {'attempts': attempts, 'request_sha256': hashlib.sha256(req).hexdigest(),
                    'response_sha256': hashlib.sha256(resp).hexdigest()}
        except (OSError, ValueError):
            time.sleep(.4)
    raise TimeoutError('independent real host memcached miss/SET/GET failed')


def original_challenge(path: Path) -> tuple[str, str]:
    if path.is_symlink() or not path.is_file():
        raise ValueError('second QEMU requires original first host receipt')
    proof = json.loads(path.read_text())
    key, value = proof.get('key'), proof.get('value')
    if proof.get('phase') != 'first' or not isinstance(key, str) or \
            not isinstance(value, str):
        raise ValueError('invalid first phase challenge')
    request, response = wire_messages(key, value)
    if proof.get('request_sha256') != hashlib.sha256(request).hexdigest() or \
            proof.get('response_sha256') != hashlib.sha256(response).hexdigest():
        raise ValueError('original first receipt digest changed')
    return key, value


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('first', 'reopen'))
    parser.add_argument('uart', type=Path)
    parser.add_argument('host_port', type=int)
    parser.add_argument('output', type=Path)
    parser.add_argument('--first-receipt', type=Path)
    parser.add_argument('--ready-timeout', type=int, default=2750)
    parser.add_argument('--cache-timeout', type=int, default=45)
    args = parser.parse_args()
    if not 1 <= args.host_port <= 65535 or args.output.exists() or args.output.is_symlink():
        parser.error('bad host port or stale receipt')
    if args.phase == 'reopen':
        if args.first_receipt is None:
            parser.error('reopen demands independent previous-QEMU receipt')
        key, original_value = original_challenge(args.first_receipt)
        value = secrets.token_hex(32)
        while value == original_value:
            value = secrets.token_hex(32)
    else:
        key, value = 'naseed_m232_' + secrets.token_hex(24), secrets.token_hex(32)
    print('[M232 HOST WAIT] original Alpine PID1 Memcached OpenRC guest TCP', flush=True)
    wait_ready(args.uart, READY[args.phase], args.ready_timeout)
    receipt = transaction(args.host_port, key, value, args.cache_timeout)
    evidence = {'phase': args.phase, 'transport': TRANSPORT,
                'host': '127.0.0.1', 'host_port': args.host_port,
                'guest_port': GUEST_PORT, 'key': key, 'value': value,
                'value_sha256': hashlib.sha256(value.encode()).hexdigest(),
                'first_key_vanished_after_cold_reboot': args.phase == 'reopen',
                'original_cache_first_get_miss': True, 'host_set_stored': True,
                'host_get_exact_value': True,
                'test_only_root_daemon_and_localhost_ingress': True,
                'timestamp_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                **receipt}
    args.output.write_text(json.dumps(evidence, sort_keys=True, indent=2) + '\n')
    print(f'[M232 HOST CACHE RECEIPT] phase={args.phase} independent real '
          'memcached miss/SET/GET, physical NIC PCAP + ext4 still REQUIRED', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (OSError, TimeoutError, ValueError, json.JSONDecodeError) as exc:
        raise SystemExit('[M232 HOST CACHE FAIL] ' + str(exc)) from exc
