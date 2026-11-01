#!/usr/bin/env python3
"""Independent host endpoint: count real HTTP GETs forwarded by guest Tinyproxy."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/proxy":
            self.send_error(404)
            return
        body = b"M149_REAL_TINYPROXY_HOST_PAYLOAD\n"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        print(f"[M149 HOST PROXY GET] {self.path} bytes={len(body)}", flush=True)

    def log_message(self, fmt, *args):
        pass


if __name__ == "__main__":
    server = ThreadingHTTPServer(("127.0.0.1", 15449), Handler)
    print("[M149 HOST HTTP READY]", flush=True)
    server.serve_forever()
