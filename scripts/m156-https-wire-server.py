#!/usr/bin/env python3
"""Independent TLS oracle for original Alpine applications inside NaSeed.

The certificate and key are created by the QEMU harness, never checked in.
Only successful TLS 1.2+ HTTP requests are recorded as guest wire evidence.
"""

import ssl
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


BODIES = {
    "/curl": b"M156_REAL_HTTPS_CURL_PAYLOAD\n",
    "/wget": b"M156_REAL_HTTPS_WGET_PAYLOAD\n",
}


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        body = BODIES.get(self.path)
        if body is None:
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()
        print(
            f"[M156 HTTPS HOST GET] {self.path} bytes={len(body)} "
            f"tls={self.connection.version()}",
            flush=True,
        )

    def log_message(self, fmt, *args):
        pass


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: m156-https-wire-server.py CERT KEY")
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(sys.argv[1], sys.argv[2])
    server = ThreadingHTTPServer(("127.0.0.1", 15443), Handler)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    print("[M156 HTTPS HOST READY] 127.0.0.1:15443 TLSv1.2+", flush=True)
    server.serve_forever()
