#!/usr/bin/env python3
from http.server import ThreadingHTTPServer,BaseHTTPRequestHandler
DATA={"/health":b"M144_HOST_HTTP_READY\n",
      "/ab":b"M144_REAL_AB_BODY\n",
      "/haproxy":b"M144_HOST_HAPROXY_BACKEND_VALUE\n"}
class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        body=DATA.get(self.path)
        if body is None:
            self.send_error(404);return
        self.send_response(200)
        self.send_header("Content-Length",str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        print(f"[M144 HOST GET] {self.path} bytes={len(body)}",flush=True)
    def log_message(self,fmt,*args):pass
if __name__=="__main__":
    server=ThreadingHTTPServer(("127.0.0.1",15441),Handler)
    print("[M144 HOST READY]",flush=True)
    server.serve_forever()
