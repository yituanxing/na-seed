#!/usr/bin/env python3
"""QEMU slirp host-only HTTP oracle for an original Alpine Ncat request."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import sys

BODY = b"M176_REAL_NCAT_HTTP_RESPONSE\n"


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/healthz":
            self.send_response(200)
            self.send_header("Content-Length", "2")
            self.end_headers()
            self.wfile.write(b"OK")
            return
        if self.path != "/m176-ncat-irq" or self.headers.get("Host") != "m176.naseed.test":
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(BODY)))
        self.end_headers()
        self.wfile.write(BODY)
        self.wfile.flush()
        print("[M176 HOST REAL NCAT REQUEST] /m176-ncat-irq", flush=True)

    def log_message(self, fmt, *args):
        print("[M176 HOST HTTP] " + (fmt % args), flush=True)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 15416
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
