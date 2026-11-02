#!/usr/bin/env python3
"""TLS image oracle reusing the existing M156 independent host handler.

The entire PNG stays on the host. The guest must fetch it through QEMU
slirp with original Alpine curl/wget and strict X.509 CA verification.
"""

from pathlib import Path
import runpy
import ssl
import sys
from http.server import ThreadingHTTPServer


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('usage: m160-image-wire-server.py CERT KEY PNG')
    image = Path(sys.argv[3]).read_bytes()
    if len(image) < 200 or not image.startswith(b'\x89PNG\r\n\x1a\n'):
        raise SystemExit('invalid PNG image oracle')

    # M156 owns the proven request handler and exact wire GET logging.
    # Its globals remain reachable by Handler.do_GET after run_path returns.
    original = runpy.run_path('scripts/m156-https-wire-server.py')
    original['BODIES'].clear()
    original['BODIES'].update({
        '/m160-curl.png': image,
        '/m160-wget.png': image,
    })

    class ImageHandler(original['Handler']):
        # Reuse M156's exact request/body/TLS logging and override only the
        # MIME type, so a genuine browser/image consumer sees image/png.
        def send_header(self, keyword, value):
            if keyword.lower() == 'content-type' and self.path in original['BODIES']:
                value = 'image/png'
            return super().send_header(keyword, value)

    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.minimum_version = ssl.TLSVersion.TLSv1_2
    tls.load_cert_chain(sys.argv[1], sys.argv[2])
    server = ThreadingHTTPServer(('127.0.0.1', 15443), ImageHandler)
    server.socket = tls.wrap_socket(server.socket, server_side=True)
    print(f'[M160 IMAGE HOST READY] 127.0.0.1:15443 bytes={len(image)} TLSv1.2+', flush=True)
    server.serve_forever()
