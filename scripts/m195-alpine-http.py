#!/usr/bin/env python3
"""Read-only, exact-path Alpine repository server for M195 QEMU slirp.

Usage: python3 scripts/m195-alpine-http.py OUTDIR 15417

Only loopback is bound: QEMU's 10.0.2.2 host alias reaches that socket.
No directory listing, redirects, filename decoding or file-system path
resolution occurs after startup. All served bytes match the staged SHA256
manifest, and HTTP logs are separate from guest UART success markers.
"""

import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re


ARCHES = ("x86_64", "riscv64", "aarch64")
ALLOW = re.compile(r"^(?:x86_64|riscv64|aarch64)/(?:APKINDEX\.tar\.gz|tree-2\.2\.1-r0\.apk)$|^proof\.txt$")
HEALTH = b"OK\n"


def load_repository(root: Path) -> tuple[str, dict[str, bytes]]:
    root = root.resolve(strict=True)
    manifests = [root / f"m195-{arch}.json" for arch in ARCHES]
    present = [path for path in manifests if path.is_file() and not path.is_symlink()]
    if len(present) != 1:
        raise ValueError("[M195 HTTP FAIL] expected exactly one target manifest")
    info = json.loads(present[0].read_text(encoding="utf-8"))
    arch = info.get("arch")
    if arch not in ARCHES or present[0].name != f"m195-{arch}.json":
        raise ValueError("[M195 HTTP FAIL] invalid target identity")
    entries = info.get("files")
    if not isinstance(entries, list) or len(entries) != 6:
        raise ValueError("[M195 HTTP FAIL] incomplete repository manifest")
    expected = {f"{arch}/APKINDEX.tar.gz", f"{arch}/tree-2.2.1-r0.apk", "proof.txt", "repositories", "index-path", "package-path"}
    if {item.get("path") for item in entries} != expected:
        raise ValueError("[M195 HTTP FAIL] unexpected repository members")
    served = {}
    for entry in entries:
        name = entry["path"]
        if not ALLOW.fullmatch(name) and name not in ("repositories", "index-path", "package-path"):
            raise ValueError("[M195 HTTP FAIL] forbidden member")
        file = root / name
        if file.is_symlink() or not file.is_file() or not file.resolve().is_relative_to(root):
            raise ValueError("[M195 HTTP FAIL] unsafe member path")
        data = file.read_bytes()
        if len(data) != entry.get("size") or hashlib.sha256(data).hexdigest() != entry.get("sha256"):
            raise ValueError("[M195 HTTP FAIL] staged bytes changed: " + name)
        if name not in ("repositories", "index-path", "package-path"):
            served["/" + name] = data
    return arch, served


def make_handler(served: dict[str, bytes]):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def send_object(self, head_only: bool) -> None:
            if self.path == "/healthz":
                data = HEALTH
                content_type = "text/plain"
            else:
                data = served.get(self.path)
                if data is None:
                    self.send_error(404, "unlisted repository path")
                    return
                content_type = "application/octet-stream"
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.end_headers()
            if not head_only:
                self.wfile.write(data)
                self.wfile.flush()
            if self.path != "/healthz":
                print(f"[M195 HOST HTTP] {'HEAD' if head_only else 'GET'} "
                      f"path={self.path} bytes={len(data)} sha256={hashlib.sha256(data).hexdigest()}",
                      flush=True)
            self.close_connection = True

        def do_GET(self) -> None:
            self.send_object(False)

        def do_HEAD(self) -> None:
            self.send_object(True)

        def log_message(self, format_string: str, *args) -> None:
            # Exact request paths are printed only after a verified 200; do
            # not let arbitrary request text enter the acceptance marker.
            pass

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("port", type=int)
    args = parser.parse_args()
    if not (1024 <= args.port <= 65535):
        parser.error("HTTP port must be unprivileged and valid")
    try:
        arch, served = load_repository(args.root)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        parser.exit(1, str(exc) + "\n")
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(served))
    server.daemon_threads = True
    print(f"[M195 HOST HTTP READY] bind=127.0.0.1 port={args.port} "
          f"arch={arch} files={len(served)}", flush=True)
    try:
        server.serve_forever(poll_interval=0.1)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
