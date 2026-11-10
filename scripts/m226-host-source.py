#!/usr/bin/env python3
"""Independent per-boot host-only challenge, NOT a restic REST backend.

The real repository is served by the official pinned rest-server binary on
port 15452.  This separate fixed GET-only endpoint publishes one unpredictable
source file over guest TCP and retains its own receipt for PCAP correlation.
"""

import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import sys

PORT = 15453
SIZES = {'first': 320, 'reopen': 416}


def source(phase: str, directory: Path) -> bytes:
    if (phase not in SIZES or directory.name != 'm226-host-' + phase or
            directory.is_symlink()):
        raise ValueError('[M226 SOURCE FAIL] phase/path mismatch')
    target = directory / 'source.bin'
    if target.is_symlink():
        raise ValueError('[M226 SOURCE FAIL] source is a symlink')
    body = target.read_bytes()
    if len(body) != SIZES[phase]:
        raise ValueError('[M226 SOURCE FAIL] invalid challenge length')
    return body


def generate(phase: str, directory: Path):
    if (phase not in SIZES or directory.name != 'm226-host-' + phase or
            directory.exists() or directory.is_symlink()):
        raise ValueError('[M226 SOURCE FAIL] repeated or invalid challenge')
    directory.mkdir(mode=0o700)
    (directory / 'source.bin').write_bytes(os.urandom(SIZES[phase]))
    print(f'[M226 SOURCE GENERATED] phase={phase} size={SIZES[phase]}', flush=True)


def serve(phase: str, directory: Path):
    body = source(phase, directory)
    digest = hashlib.sha256(body).hexdigest()

    class Challenge(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path != '/m226.source':
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Connection', 'close')
            self.end_headers()
            self.wfile.write(body)
            print(f'M226_SOURCE_GET phase={phase} bytes={len(body)} '
                  f'sha256={digest}', flush=True)

        def log_message(self, *_args):
            return

    with ThreadingHTTPServer(('127.0.0.1', PORT), Challenge) as server:
        print(f'[M226 SOURCE READY] phase={phase} port={PORT}', flush=True)
        server.serve_forever()


if __name__ == '__main__':
    if len(sys.argv) != 4 or sys.argv[1] not in ('generate', 'serve'):
        raise SystemExit('usage: m226-host-source.py generate|serve first|reopen DIR')
    mode, boot_phase, root = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    if mode == 'generate':
        generate(boot_phase, root)
    else:
        serve(boot_phase, root)
