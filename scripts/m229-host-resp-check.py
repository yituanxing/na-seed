#!/usr/bin/env python3
"""Independent host -> real Alpine Valkey RESP challenge for each native QEMU.

The caller must keep QEMU alive, supply a fresh 256-bit disposable password
through NAS_M229_RESP_PASSWORD, and preserve the *real* VirtIO-Net PCAP. This
receipt alone is NOT native-QEMU, OpenRC or physical-ext4 acceptance.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import socket
import sys
import time


GUEST_PORT = 15445
TRANSPORT = 'host-localhost-QEMU-user-hostfwd-guest-eth0-valkey'
READY = {
    'first': b'M229_FIRST_HOST_RESP_READY',
    'reopen': b'M229_REOPEN_HOST_RESP_READY',
}
HEX_64 = re.compile(r'[0-9a-f]{64}\Z')
HEX_48 = re.compile(r'[0-9a-f]{48}\Z')


def valid_port(raw: str) -> int:
    stripped = raw.lstrip('0')
    if (not raw.isascii() or not raw.isdecimal() or
            not stripped or len(stripped) > 5):
        raise ValueError('host port must be decimal 1..65535')
    port = int(stripped)
    if port > 65535:
        raise ValueError('host port must be decimal 1..65535')
    return port


def token_from_env() -> str:
    token = os.environ.get('NAS_M229_RESP_PASSWORD', '')
    if HEX_64.fullmatch(token) is None:
        raise ValueError('M229 requires fresh 64-hex disposable Valkey AUTH token')
    return token


def wait_for_guest_ready(serial: Path, marker: bytes, seconds: int) -> None:
    deadline = time.monotonic() + seconds
    position = 0
    tail = b''
    while time.monotonic() < deadline:
        try:
            with serial.open('rb') as stream:
                if stream.seek(0, 2) < position:
                    position, tail = 0, b''
                stream.seek(position)
                chunk = stream.read()
                position = stream.tell()
        except FileNotFoundError:
            chunk = b''
        if chunk:
            combined = tail + chunk
            if any(line.strip() == marker for line in
                   combined.replace(b'\r', b'').splitlines()):
                return
            tail = combined[-max(256, len(marker) * 2):]
        time.sleep(0.2)
    raise TimeoutError('original Alpine PID1 did not emit M229 Valkey-ready UART')


def command(*arguments: str) -> bytes:
    output = [f'*{len(arguments)}\r\n'.encode('ascii')]
    for argument in arguments:
        raw = argument.encode('ascii')
        output.append(f'${len(raw)}\r\n'.encode('ascii'))
        output.append(raw + b'\r\n')
    return b''.join(output)


def read_reply(stream) -> bytes:
    """Read exactly one RESP simple-string or bulk-string reply, bounded."""
    head = stream.readline(4097)
    if not head.endswith(b'\r\n') or len(head) > 4096:
        raise ValueError('Valkey RESP reply missing bounded CRLF')
    if head.startswith(b'-'):
        raise ValueError('Valkey error reply (AUTH, service or persistence failure)')
    if head.startswith(b'+'):
        return head
    if not head.startswith(b'$'):
        raise ValueError('Valkey RESP reply has unsupported type')
    length = head[1:-2]
    if not length.isascii() or not length.isdigit():
        raise ValueError('Valkey RESP returned nil/invalid bulk length')
    size = int(length)
    if size > 1024:
        raise ValueError('Valkey RESP bulk reply exceeded safe bound')
    data = stream.read(size + 2)
    if len(data) != size + 2 or not data.endswith(b'\r\n'):
        raise ValueError('Valkey RESP bulk reply truncated')
    return head + data


def read_first_receipt(path: Path) -> tuple[str, str, str]:
    if path.is_symlink() or not path.is_file():
        raise ValueError('second native QEMU requires independent first receipt')
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError('malformed first-native-QEMU host receipt') from exc
    if not isinstance(data, dict) or data.get('phase') != 'first':
        raise ValueError('reopen input is not a real first-phase host receipt')
    nonce, key, value = data.get('nonce'), data.get('key'), data.get('value')
    if (not isinstance(nonce, str) or HEX_48.fullmatch(nonce) is None or
            key != 'm229:' + nonce or not isinstance(value, str) or
            HEX_64.fullmatch(value) is None):
        raise ValueError('reopen first-phase challenge/key/value malformed')
    if data.get('value_sha256') != hashlib.sha256(value.encode()).hexdigest():
        raise ValueError('reopen first-phase value hash mismatch')
    return nonce, key, value


def transaction(port: int, phase: str, token: str, key: str, value: str,
                nonce: str, seconds: int) -> dict:
    """One real host TCP session; AUTH before all original Valkey commands."""
    sequence = [('AUTH', token), ('PING',), ('ECHO', nonce)]
    if phase == 'first':
        sequence += [('SET', key, value), ('GET', key), ('SAVE',)]
    else:
        sequence += [('GET', key)]
    request = b''.join(command(*row) for row in sequence)
    expected = [b'+OK\r\n', b'+PONG\r\n',
                f'${len(nonce)}\r\n{nonce}\r\n'.encode()]
    if phase == 'first':
        expected.append(b'+OK\r\n')
    expected.append(f'${len(value)}\r\n{value}\r\n'.encode())
    if phase == 'first':
        expected.append(b'+OK\r\n')
    deadline = time.monotonic() + seconds
    attempts = 0
    last_error = 'no socket connection'
    while time.monotonic() < deadline:
        attempts += 1
        try:
            with socket.create_connection(('127.0.0.1', port), timeout=3) as sock:
                sock.settimeout(8)
                sock.sendall(request)
                with sock.makefile('rb') as stream:
                    actual = [read_reply(stream) for _ in expected]
            if actual != expected:
                raise ValueError('original Valkey authenticated RESP content differs')
            return {
                'phase': phase,
                'transport': TRANSPORT,
                'host': '127.0.0.1',
                'host_port': port,
                'guest_port': GUEST_PORT,
                'nonce': nonce,
                'key': key,
                'value': value,
                'value_sha256': hashlib.sha256(value.encode()).hexdigest(),
                'auth_required': True,
                'commands': [row[0] for row in sequence],
                'request_sha256': hashlib.sha256(request).hexdigest(),
                'response_sha256': hashlib.sha256(b''.join(actual)).hexdigest(),
                'attempts': attempts,
                'timestamp_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
            }
        except (OSError, ValueError) as exc:
            # Do not log a Valkey error or the AUTH request: either might
            # contain the disposable authentication token.
            last_error = type(exc).__name__
        time.sleep(0.4)
    raise TimeoutError('real host TCP authenticated Valkey RESP failed: ' + last_error)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('first', 'reopen'))
    parser.add_argument('uart', type=Path)
    parser.add_argument('host_port', type=valid_port)
    parser.add_argument('receipt', type=Path)
    parser.add_argument('--first-receipt', type=Path)
    parser.add_argument('--ready-timeout', type=int, default=2650)
    parser.add_argument('--resp-timeout', type=int, default=50)
    args = parser.parse_args()
    if args.ready_timeout < 1 or args.resp_timeout < 1:
        parser.error('timeouts must be positive')
    if args.receipt.exists() or args.receipt.is_symlink():
        parser.error('refusing to overwrite host receipt')
    if args.phase == 'reopen' and args.first_receipt is None:
        parser.error('second native QEMU requires --first-receipt')
    token = token_from_env()
    if args.phase == 'first':
        nonce = secrets.token_hex(24)
        key, value = 'm229:' + nonce, secrets.token_hex(32)
    else:
        first_nonce, key, value = read_first_receipt(args.first_receipt)
        nonce = secrets.token_hex(24)
        if nonce == first_nonce:
            raise ValueError('host nonce reused after independent native QEMU reboot')
    print(f'[M229 HOST WAIT] phase={args.phase} original Alpine guest READY', flush=True)
    wait_for_guest_ready(args.uart, READY[args.phase], args.ready_timeout)
    print(f'[M229 HOST READY] phase={args.phase} sending authenticated RESP', flush=True)
    result = transaction(args.host_port, args.phase, token, key, value, nonce,
                         args.resp_timeout)
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    with args.receipt.open('x', encoding='utf-8') as output:
        json.dump(result, output, sort_keys=True, indent=2)
        output.write('\n')
    print(f'[M229 HOST RESP RECEIPT] phase={args.phase} original Valkey AUTH '
          'PING/ECHO/GET (+SET/SAVE on first); PCAP/ext4 still required', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (OSError, TimeoutError, ValueError) as exc:
        raise SystemExit('[M229 HOST RESP FAIL] ' + str(exc)) from exc
