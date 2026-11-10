#!/usr/bin/env python3
"""HOST UNIT ONLY: exercise M228 watcher and TCP parser, not QEMU acceptance."""

import importlib.util
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import tempfile
from threading import Thread
import time


source = Path(__file__).with_name('m228-host-inbound-check.py')
spec = importlib.util.spec_from_file_location('m228_host_tcp_unit', source)
assert spec and spec.loader
owner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owner)


def check() -> None:
    for invalid in ('', '0', '00000', '65536', '1,hostfwd=', '-1', ' 2', '2\n',
                    '1.0', '0x50', '１２３'):
        try:
            owner.valid_port(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError('unsafe port accepted: ' + repr(invalid))
    assert owner.valid_port('1') == 1
    assert owner.valid_port('65535') == 65535

    with tempfile.TemporaryDirectory(prefix='m228-host-unit-') as directory:
        serial = Path(directory) / 'serial.log'
        marker = owner.READY['first']

        def append_real_line() -> None:
            time.sleep(0.15)
            serial.write_bytes(b'echo ' + marker + b' is NOT a real guest line\r\n'
                               + marker[:10])
            time.sleep(0.15)
            with serial.open('ab') as log:
                log.write(marker[10:] + b'\r\n')

        writer = Thread(target=append_real_line)
        writer.start()
        owner.wait_for_real_guest(serial, marker, 3)
        writer.join()

        class ExactGuestUnitFixture(BaseHTTPRequestHandler):
            def do_GET(self):
                if not self.path.startswith('/index.txt?m228_phase=first&nonce='):
                    self.send_error(404)
                    return
                self.send_response(200)
                self.end_headers()
                self.wfile.write(owner.BODY)

            def log_message(self, *_args):
                pass

        server = ThreadingHTTPServer(('127.0.0.1', 0), ExactGuestUnitFixture)
        thread = Thread(target=server.serve_forever)
        thread.start()
        try:
            receipt = owner.real_host_get(server.server_address[1], 'first', 3)
            assert receipt['http_status'] == 200
            assert receipt['body_length'] == len(owner.BODY)
            assert receipt['http_path'].startswith('/index.txt?m228_phase=first&nonce=')
            assert receipt['transport'] == 'host-localhost-QEMU-user-hostfwd-guest-eth0'
        finally:
            server.shutdown()
            server.server_close()
            thread.join()
    print('[M228 HOST UNIT PASS] strict ports, split UART readiness and '
          'exact HTTP parser; NOT real QEMU host-ingress acceptance')


if __name__ == '__main__':
    check()
