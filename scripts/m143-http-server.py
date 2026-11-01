#!/usr/bin/env python3
"""Independent host HTTP wire oracle. Only original guest clients may fetch."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

BODIES = {
    "/wget": b"M143_REAL_HTTP_WGET_PAYLOAD\n",
    "/aria2": b"M143_REAL_HTTP_ARIA2_PAYLOAD\n",
    "/curl": b"M143_REAL_HTTP_CURL_PAYLOAD\n",
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
        print(f"[M143 HOST HTTP GET] {self.path} bytes={len(body)}", flush=True)

    def log_message(self, fmt, *args):
        pass

if __name__ == "__main__":
    server = ThreadingHTTPServer(("127.0.0.1", 15431), Handler)
    print("[M143 HOST HTTP READY] 127.0.0.1:15431", flush=True)
    server.serve_forever()
