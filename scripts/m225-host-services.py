#!/usr/bin/env python3
"""Bounded, real host-side FTP and HTTP transfer witnesses for M225.

Only the three fixed challenge objects are served.  Their unpredictable bytes
are created *after* guest staging, never copied into the guest rootfs.  This
is an integration fixture, not a general FTP or restic REST server.
"""

import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import signal
import socket
from socketserver import BaseRequestHandler, ThreadingTCPServer
import sys
from threading import Event, Thread

PHASES = ('first', 'reopen')
FTP_ITEMS = ('lftp', 'rclone')
ITEMS = FTP_ITEMS + ('restic',)
SIZES = {'lftp': 160, 'rclone': 192, 'restic': 256}
FTP_PORT, REST_PORT = 15451, 15452


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source(phase: str, directory: Path, item: str) -> bytes:
    if (phase not in PHASES or item not in ITEMS or
            directory.name != 'm225-host-' + phase or directory.is_symlink()):
        raise ValueError('[M225 HOST FAIL] invalid source phase/path')
    path = directory / (item + '.source')
    if path.is_symlink():
        raise ValueError('[M225 HOST FAIL] symlinked source')
    data = path.read_bytes()
    if len(data) != SIZES[item]:
        raise ValueError('[M225 HOST FAIL] wrong random challenge size')
    return data


def generate(phase: str, directory: Path) -> None:
    if (phase not in PHASES or directory.name != 'm225-host-' + phase or
            directory.exists() or directory.is_symlink()):
        raise ValueError('[M225 HOST FAIL] stale/invalid generated directory')
    directory.mkdir(mode=0o700)
    for item in ITEMS:
        (directory / (item + '.source')).write_bytes(os.urandom(SIZES[item]))
    print(f'[M225 HOST GENERATED] phase={phase} three host-only random sources')


def upload(phase: str, directory: Path, item: str, data: bytes) -> None:
    if data != source(phase, directory, item):
        raise ValueError('[M225 HOST DENY] actual received bytes differ from challenge')
    path = directory / (item + '.upload')
    with path.open('xb') as out:  # exclusive, never overwrite first receipt
        out.write(data)
    print(f'M225_HOST_UPLOAD phase={phase} item={item} bytes={len(data)} '
          f'sha256={digest(data)}', flush=True)


class TCPServer(ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class HTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True
    daemon_threads = True


def servers(phase: str, directory: Path, *, ftp_port=FTP_PORT, rest_port=REST_PORT):
    """Return actual bound sockets; callers own shutdown/server_close."""
    for item in ITEMS:
        source(phase, directory, item)

    class FTP(BaseRequestHandler):
        def handle(self):
            self.request.settimeout(15)
            stream = self.request.makefile('rb')
            passive = None

            def respond(code, message):
                self.request.sendall(f'{code} {message}\r\n'.encode())

            respond(220, 'M225 bounded binary FTP fixture')
            try:
                for _ in range(80):
                    line = stream.readline(1024)
                    if not line or len(line) >= 1024:
                        break
                    parts = line.decode('ascii', 'replace').strip().split(' ', 1)
                    verb, argument = parts[0].upper(), parts[1] if len(parts) == 2 else ''
                    if verb == 'USER':
                        respond(331, 'Password required')
                    elif verb == 'PASS':
                        respond(230, 'Logged in')
                    elif verb == 'TYPE':
                        respond(200, 'Type set')
                    elif verb in ('SYST',):
                        respond(215, 'UNIX Type: L8')
                    elif verb == 'PWD':
                        respond(257, '"/" current directory')
                    elif verb == 'NOOP':
                        respond(200, 'OK')
                    elif verb == 'FEAT':
                        self.request.sendall(b'211-Features\r\n EPSV\r\n SIZE\r\n211 End\r\n')
                    elif verb in ('EPSV', 'PASV'):
                        if passive is not None:
                            passive.close()
                        passive = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                        passive.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                        passive.bind(('127.0.0.1', 0))
                        passive.listen(1)
                        passive.settimeout(15)
                        port = passive.getsockname()[1]
                        if verb == 'EPSV':
                            respond(229, f'Entering Extended Passive Mode (|||{port}|)')
                        else:
                            respond(227, f'Entering Passive Mode (10,0,2,2,{port >> 8},{port & 255})')
                        print(f'M225_HOST_FTP_PASSIVE phase={phase} port={port}', flush=True)
                    elif verb in ('RETR', 'STOR', 'LIST'):
                        item = argument.lstrip('/').removesuffix('.source').removesuffix('.upload')
                        exact = f'{item}.source' if verb == 'RETR' else f'{item}.upload'
                        listing = verb == 'LIST' and argument in ('', '.', '/', './')
                        if (passive is None or not listing and
                                (item not in FTP_ITEMS or argument.lstrip('/') != exact)):
                            respond(550, 'Fixed challenge path or passive channel required')
                            continue
                        respond(150, 'Opening binary data channel')
                        data = bytearray()
                        try:
                            with passive.accept()[0] as channel:
                                channel.settimeout(15)
                                if listing:
                                    rows = [f'-rw-r--r-- 1 m225 m225 {SIZES[name]} '
                                            f'Jan 01 00:00 {name}.source\r\n'
                                            for name in FTP_ITEMS]
                                    rows.extend(f'-rw-r--r-- 1 m225 m225 {SIZES[name]} '
                                                f'Jan 01 00:00 {name}.upload\r\n'
                                                for name in FTP_ITEMS
                                                if (directory / f'{name}.upload').is_file())
                                    data = bytearray(''.join(rows).encode())
                                    channel.sendall(data)
                                elif verb == 'RETR':
                                    data = bytearray(source(phase, directory, item))
                                    channel.sendall(data)
                                else:
                                    while len(data) <= 4096:
                                        chunk = channel.recv(4097 - len(data))
                                        if not chunk:
                                            break
                                        data.extend(chunk)
                            if verb == 'STOR':
                                upload(phase, directory, item, bytes(data))
                            respond(226, 'Binary transfer complete')
                            if listing:
                                print(f'[M225 HOST FTP LIST] phase={phase} '
                                      f'bytes={len(data)}', flush=True)
                            else:
                                print(f'M225_HOST_FTP phase={phase} op={verb} item={item} '
                                      f'bytes={len(data)} sha256={digest(data)}', flush=True)
                        except (OSError, ValueError) as exc:
                            print(f'[M225 HOST DENY] FTP {verb} item={item}: '
                                  f'{type(exc).__name__}', flush=True)
                            respond(550, 'Binary transaction failed')
                        finally:
                            passive.close()
                            passive = None
                    elif verb == 'SIZE' and argument.lstrip('/') in [f'{i}.source' for i in FTP_ITEMS]:
                        respond(213, str(SIZES[argument.lstrip('/').split('.')[0]]))
                    elif verb == 'SIZE' and argument.lstrip('/') in [f'{i}.upload' for i in FTP_ITEMS] \
                            and (directory / argument.lstrip('/')).is_file():
                        respond(213, str((directory / argument.lstrip('/')).stat().st_size))
                    elif verb == 'MDTM' and argument.lstrip('/') in \
                            [f'{i}.{kind}' for i in FTP_ITEMS for kind in ('source', 'upload')]:
                        respond(213, '20261107000000')
                    elif verb == 'QUIT':
                        respond(221, 'Goodbye')
                        break
                    else:
                        print(f'[M225 HOST FTP UNSUPPORTED] verb={verb} '
                              f'argument={argument[:80]!r}', flush=True)
                        respond(502, 'Unsupported bounded fixture command')
            finally:
                if passive is not None:
                    passive.close()
                stream.close()

    class REST(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'

        def reply(self, code, data=b''):
            self.send_response(code)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Connection', 'close')
            self.end_headers()
            if data:
                self.wfile.write(data)

        def do_GET(self):
            if self.path != '/restic.source':
                self.reply(404)
                return
            data = source(phase, directory, 'restic')
            self.reply(200, data)
            print(f'M225_HOST_REST phase={phase} op=GET item=restic '
                  f'bytes={len(data)} sha256={digest(data)}', flush=True)

        def do_PUT(self):
            if self.path != '/restic.upload':
                self.reply(404)
                return
            size = self.headers.get('Content-Length', '')
            if not size.isdecimal() or int(size) > 4096:
                self.reply(413)
                return
            data = self.rfile.read(int(size))
            try:
                upload(phase, directory, 'restic', data)
            except (OSError, ValueError):
                self.reply(409)
                return
            self.reply(201)
            print(f'M225_HOST_REST phase={phase} op=PUT item=restic '
                  f'bytes={len(data)} sha256={digest(data)}', flush=True)

        def log_message(self, *_args):
            return

    ftp = TCPServer(('127.0.0.1', ftp_port), FTP)
    try:
        rest = HTTPServer(('127.0.0.1', rest_port), REST)
    except Exception:
        ftp.server_close()
        raise
    return ftp, rest


def serve(phase: str, directory: Path) -> None:
    ftp, rest = servers(phase, directory)
    done = Event()
    signal.signal(signal.SIGINT, lambda *_: done.set())
    signal.signal(signal.SIGTERM, lambda *_: done.set())
    threads = [Thread(target=instance.serve_forever, daemon=True)
               for instance in (ftp, rest)]
    try:
        for thread in threads:
            thread.start()
        print(f'[M225 HOST READY] phase={phase} ftp={FTP_PORT} rest={REST_PORT}', flush=True)
        done.wait()
    finally:
        for instance in (ftp, rest):
            instance.shutdown()
            instance.server_close()


if __name__ == '__main__':
    if len(sys.argv) != 4 or sys.argv[1] not in ('generate', 'serve'):
        raise SystemExit('usage: m225-host-services.py generate|serve first|reopen HOST_DIR')
    mode, phase, directory = sys.argv[1], sys.argv[2], Path(sys.argv[3])
    if mode == 'generate':
        generate(phase, directory)
    else:
        serve(phase, directory)
