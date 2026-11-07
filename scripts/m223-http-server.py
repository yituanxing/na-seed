#!/usr/bin/env python3
"""Real per-boot host HTTP service, fresh random bytes, audit every HTTP GET.

Generated sources are host-only and NEVER copied into the signed Alpine root.
The guest cannot obtain the expected bytes except through its original HTTP
client against QEMU user-network gateway 10.0.2.2:15431.
"""
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import sys

PORT = 15431
ROUTES = ('wget', 'aria2')


def source(phase: str, route: str) -> bytes:
    if phase not in ('first', 'reopen') or route not in ROUTES:
        raise ValueError('invalid phase/route')
    return f'M223_{phase.upper()}_{route.upper()}_SOURCE_'.encode() + os.urandom(96).hex().encode() + b'\n'


def generate(phase: str, directory: Path) -> None:
    if directory.exists() or directory.is_symlink():
        raise ValueError('stale or symlinked host-only source directory')
    directory.mkdir(mode=0o700)
    for route in ROUTES:
        (directory / f'{route}.source').write_bytes(source(phase, route))
    print(f'[M223 HOST GENERATED] phase={phase} two fresh unpredictable sources')


def serve(phase: str, directory: Path) -> None:
    if phase not in ('first', 'reopen') or directory.is_symlink():
        raise ValueError('wrong-phase or symlinked host directory')
    bodies = {}
    for route in ROUTES:
        file = directory / f'{route}.source'
        if file.is_symlink():
            raise ValueError('symlinked host random source')
        body = file.read_bytes()
        if not re.fullmatch(rb'M223_' + phase.upper().encode() + rb'_' + route.upper().encode() + rb'_SOURCE_[0-9a-f]{192}\n', body):
            raise ValueError('invalid host-only random source identity')
        bodies['/' + route] = body
    if len(set(bodies.values())) != len(ROUTES):
        raise ValueError('duplicate host sources')

    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'

        def do_GET(self):
            body = bodies.get(self.path)
            if body is None:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(body)
            self.wfile.flush()
            print(f'M223_HOST_GET phase={phase} path={self.path} status=200 '
                  f'bytes={len(body)} sha256={hashlib.sha256(body).hexdigest()} '
                  f'peer={self.client_address[0]}', flush=True)

        def log_message(self, format, *args):
            # Never expose arbitrary request headers/credentials to artifacts.
            return

    class Server(ThreadingHTTPServer):
        daemon_threads = True
        # First and reopen QEMUs need the same gateway port; prior TCP
        # sockets can linger in TIME_WAIT despite the server being stopped.
        allow_reuse_address = True

    with Server(('127.0.0.1', PORT), Handler) as server:
        print(f'[M223 HOST READY] phase={phase} port={PORT}', flush=True)
        server.serve_forever(poll_interval=0.1)


if __name__ == '__main__':
    if len(sys.argv) != 4 or sys.argv[1] not in ('generate', 'serve'):
        raise SystemExit('usage: m223-http-server.py generate|serve first|reopen HOST_DIR')
    mode, phase, directory = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    if mode == 'generate':
        generate(phase, directory)
    else:
        serve(phase, directory)
