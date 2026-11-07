#!/usr/bin/env python3
"""Independent HOST TCP ingress receipt while a real Alpine QEMU is alive.

The guest must first emit its M228 readiness marker from inside the original
PID1 session. This process then issues a fresh host -> QEMU hostfwd -> guest
eth0 -> original lighttpd HTTP GET. There is no host-side HTTP responder and
no success fallback. A separate physical NIC PCAP and ext4 oracle are required
for final M228 acceptance; this one receipt is never sufficient by itself.
"""

import argparse
import hashlib
import http.client
import json
from pathlib import Path
import secrets
import sys
import time


BODY = b'M227_OPENRC_HTTP_DURABLE_2026\n'
READY = {'first': b'M228_FIRST_HOST_INGRESS_READY',
         'reopen': b'M228_REOPEN_HOST_INGRESS_READY'}


def valid_port(raw: str) -> int:
    if not raw.isascii() or not raw.isdecimal() or raw.startswith('0'):
        raise ValueError('hostfwd TCP port must be decimal 1..65535')
    port = int(raw)
    if not 1 <= port <= 65535:
        raise ValueError('hostfwd TCP port must be decimal 1..65535')
    return port


def wait_for_real_guest(serial: Path, marker: bytes, seconds: int) -> None:
    """Read only newly appended UART bytes; no assumed ready time."""
    deadline = time.monotonic() + seconds
    position = 0
    tail = b''
    while time.monotonic() < deadline:
        try:
            with serial.open('rb') as stream:
                if stream.seek(0, 2) < position:
                    # A second independent QEMU may truncate a prior log.
                    position = 0
                    tail = b''
                stream.seek(position)
                block = stream.read()
                position = stream.tell()
        except FileNotFoundError:
            block = b''
        if block:
            payload = tail + block
            # Require a real guest line, not a substring of an echoed command.
            for line in payload.replace(b'\r', b'').splitlines():
                if line.strip() == marker:
                    return
            tail = payload[-max(512, 2 * len(marker)):]
        time.sleep(0.2)
    raise TimeoutError('M228 guest original PID1 did not emit readiness marker')


def real_host_get(port: int, phase: str, seconds: int) -> dict:
    """Run only after guest-reported readiness; no host-side proxy/client."""
    nonce = secrets.token_hex(20)
    path = f'/index.txt?m228_phase={phase}&nonce={nonce}'
    deadline = time.monotonic() + seconds
    attempts = 0
    last_error = 'no connection'
    while time.monotonic() < deadline:
        attempts += 1
        connection = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
        try:
            connection.request('GET', path, headers={
                'Host': 'naseed-m228-guest',
                'Cache-Control': 'no-store',
                'Connection': 'close',
            })
            result = connection.getresponse()
            body = result.read(4096)
            server = result.getheader('Server', '')
            if result.status != 200:
                raise RuntimeError(f'HTTP status {result.status}, expected 200')
            if body != BODY:
                raise RuntimeError('actual host TCP body differs from guest ext4 document')
            return {
                'phase': phase,
                'transport': 'host-localhost-QEMU-user-hostfwd-guest-eth0',
                'host': '127.0.0.1',
                'host_port': port,
                'guest_port': 15432,
                'http_path': path,
                'http_status': result.status,
                'body_sha256': hashlib.sha256(body).hexdigest(),
                'body_length': len(body),
                'http_server_header': server,
                'attempts': attempts,
                'timestamp_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
            }
        except (OSError, http.client.HTTPException, RuntimeError) as exc:
            last_error = str(exc)
        finally:
            connection.close()
        time.sleep(0.4)
    raise TimeoutError(f'M228 host TCP HTTP GET never passed: {last_error}')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('first', 'reopen'))
    parser.add_argument('serial', type=Path)
    parser.add_argument('host_port', type=valid_port)
    parser.add_argument('receipt', type=Path)
    parser.add_argument('--ready-timeout', type=int, default=2650)
    parser.add_argument('--http-timeout', type=int, default=35)
    args = parser.parse_args()
    if args.ready_timeout < 1 or args.http_timeout < 1:
        parser.error('both timeouts must be positive')
    if args.receipt.exists() or args.receipt.is_symlink():
        parser.error('refusing to overwrite preexisting host ingress receipt')
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    print(f'[M228 HOST WAIT] phase={args.phase} real UART marker', flush=True)
    wait_for_real_guest(args.serial, READY[args.phase], args.ready_timeout)
    print(f'[M228 HOST READY] phase={args.phase} original guest emitted marker', flush=True)
    receipt = real_host_get(args.host_port, args.phase, args.http_timeout)
    with args.receipt.open('x', encoding='utf-8') as stream:
        json.dump(receipt, stream, indent=2, sort_keys=True)
        stream.write('\n')
    print(f'[M228 HOST REAL TCP PASS] phase={args.phase} '
          f'HTTP 200 bytes={receipt["body_length"]} '
          f'body_sha256={receipt["body_sha256"]} '
          f'nonce_path={receipt["http_path"]}', flush=True)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (TimeoutError, ValueError, OSError) as exc:
        print(f'[M228 HOST FAIL] {exc}', file=sys.stderr, flush=True)
        sys.exit(1)
