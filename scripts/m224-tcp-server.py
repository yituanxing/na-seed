#!/usr/bin/env python3
"""Per-boot *host-side* TCP witnesses for original Nmap and Ncat.

The random response is generated on the host after the signed guest root is
staged. It never enters the prepared guest. Nmap's open TCP port and Ncat's
full duplex random exchange are actual QEMU user-network TCP sessions.
"""

import hashlib
import os
from pathlib import Path
import signal
import socket
from socketserver import BaseRequestHandler, ThreadingTCPServer
import sys
from threading import Event, Thread

NMAP_OPEN = 15441
NMAP_CLOSED = 15442
NCAT = 15443
PHASES = ('first', 'reopen')


def phase_token(phase: str) -> bytes:
    if phase not in PHASES:
        raise ValueError('invalid M224 phase')
    return f'M224_NCAT_CLIENT_{phase}\n'.encode()


def generate(phase: str, directory: Path) -> None:
    if (phase not in PHASES or directory.name != 'm224-host-' + phase or
            directory.exists() or directory.is_symlink()):
        raise ValueError('[M224 HOST FAIL] stale/symlinked host source directory')
    directory.mkdir(mode=0o700)
    # Exactly 96 truly unpredictable raw bytes; the signed guest only knows
    # the size, never the content. The phase identity lives in host logs and
    # independent source directory, and the disk oracle rejects reused bytes.
    source = os.urandom(96)
    (directory / 'ncat.source').write_bytes(source)
    print(f'[M224 HOST GENERATED] phase={phase} random source only on host')


def source_bytes(phase: str, directory: Path) -> bytes:
    if (phase not in PHASES or directory.name != 'm224-host-' + phase or
            directory.is_symlink()):
        raise ValueError('[M224 HOST FAIL] wrong-phase or symlinked source dir')
    file = directory / 'ncat.source'
    if file.is_symlink():
        raise ValueError('[M224 HOST FAIL] symlinked source')
    data = file.read_bytes()
    if len(data) != 96:
        raise ValueError('[M224 HOST FAIL] host-only random source not 96 bytes')
    return data


class Server(ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True


def serve(phase: str, directory: Path) -> None:
    source = source_bytes(phase, directory)
    token = phase_token(phase)

    # A host service on the supposedly CLOSED target would invalidate Nmap's
    # actual open-vs-closed contrast. Reject it before running the guest.
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(('127.0.0.1', NMAP_CLOSED))

    class NmapHandler(BaseRequestHandler):
        def handle(self):
            if self.client_address[0] != '127.0.0.1':
                return
            print(f'M224_HOST_NMAP_ACCEPT phase={phase} port={NMAP_OPEN} '
                  f'peer={self.client_address[0]}', flush=True)

    class NcatHandler(BaseRequestHandler):
        def handle(self):
            self.request.settimeout(12)
            request = bytearray()
            while len(request) < 256 and b'\n' not in request:
                data = self.request.recv(256 - len(request))
                if not data:
                    break
                request.extend(data)
            if bytes(request) != token or self.client_address[0] != '127.0.0.1':
                print(f'[M224 HOST DENY] phase={phase} invalid ncat request', flush=True)
                return
            self.request.sendall(source)
            print(f'M224_HOST_NCAT_EXCHANGE phase={phase} port={NCAT} '
                  f'rx_bytes={len(token)} rx_sha256={hashlib.sha256(token).hexdigest()} '
                  f'tx_bytes={len(source)} tx_sha256={hashlib.sha256(source).hexdigest()} '
                  f'peer={self.client_address[0]}', flush=True)

    stop = Event()

    def set_stop(_signum, _frame):
        stop.set()

    signal.signal(signal.SIGTERM, set_stop)
    signal.signal(signal.SIGINT, set_stop)
    with Server(('127.0.0.1', NMAP_OPEN), NmapHandler) as nmap, \
            Server(('127.0.0.1', NCAT), NcatHandler) as ncat:
        threads = [Thread(target=server.serve_forever, kwargs={'poll_interval': 0.1}, daemon=True)
                   for server in (nmap, ncat)]
        for thread in threads:
            thread.start()
        print(f'[M224 HOST READY] phase={phase} nmap={NMAP_OPEN} '
              f'closed={NMAP_CLOSED} ncat={NCAT}', flush=True)
        stop.wait()
        nmap.shutdown()
        ncat.shutdown()


if __name__ == '__main__':
    if len(sys.argv) != 4 or sys.argv[1] not in ('generate', 'serve'):
        raise SystemExit('usage: m224-tcp-server.py generate|serve first|reopen HOST_DIR')
    mode, boot_phase, host_dir = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    if mode == 'generate':
        generate(boot_phase, host_dir)
    else:
        serve(boot_phase, host_dir)
