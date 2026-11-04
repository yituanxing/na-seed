#!/usr/bin/env python3
"""Offline M195 signed-container/host HTTP and path-safety contracts."""

import gzip
import hashlib
from http.server import ThreadingHTTPServer
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import threading
import urllib.error
import urllib.request


def module(name: str, filename: str):
    path = Path(__file__).with_name(filename)
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


repo = module("m195_repo", "m195-alpine-repository.py")
http = module("m195_http", "m195-alpine-http.py")


def tar_bytes(files: dict[str, bytes]) -> bytes:
    stream = io.BytesIO()
    with tarfile.open(fileobj=stream, mode="w:") as archive:
        for name, data in files.items():
            info = tarfile.TarInfo(name=name)
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))
    return stream.getvalue()


def signed(*segments: dict[str, bytes]) -> bytes:
    return b"".join(gzip.compress(tar_bytes(item), mtime=0) for item in segments)


def check() -> None:
    arch = "riscv64"
    index = signed({".SIGN.RSA.test": b"signer-placeholder"},
                   {"DESCRIPTION": b"offline fixture",
                    "APKINDEX": b"P:tree\nV:2.2.1-r0\nA:riscv64\nC:Q1test\n\n"})
    package = signed({".SIGN.RSA.test": b"signer-placeholder"},
                     {".PKGINFO": b"pkgname = tree\npkgver = 2.2.1-r0\narch = riscv64\n"},
                     {"usr/bin/tree": b"ELF-placeholder"})
    assert repo.index_record(index, arch)["P"] == "tree"
    repo.validate_package(package, arch)
    for payload, checker in (
        (signed({"APKINDEX": b"P:tree\nV:2.2.1-r0\nC:Q1test\n\n"}),
         lambda value: repo.index_record(value, arch)),
        (signed({".SIGN.RSA.test": b"sig"},
                {".PKGINFO": b"pkgname = wrong\npkgver = 2.2.1-r0\narch = riscv64\n"}),
         lambda value: repo.validate_package(value, arch)),
    ):
        try:
            checker(payload)
        except ValueError:
            pass
        else:
            raise AssertionError("unsigned or wrong-package fixture accepted")

    with tempfile.TemporaryDirectory(prefix="naseed-m195-host-") as directory:
        root = Path(directory)
        details = repo.manifest("riscv64", index, package)
        files = {
            f"{arch}/APKINDEX.tar.gz": index,
            f"{arch}/tree-2.2.1-r0.apk": package,
            "proof.txt": repo.PROOF,
            "repositories": b"http://10.0.2.2:15417\n",
            "index-path": b"riscv64/APKINDEX.tar.gz\n",
            "package-path": b"riscv64/tree-2.2.1-r0.apk\n",
        }
        for name, data in files.items():
            repo.write_atomic(root / name, data)
        import json
        repo.write_atomic(root / "m195-riscv64.json",
                          (json.dumps(details) + "\n").encode())
        assert repo.check_tree(root, "riscv64")["package"] == "tree"
        _, served = http.load_repository(root)
        assert len(served) == 3
        assert "/repositories" not in served

        server = ThreadingHTTPServer(("127.0.0.1", 0), http.make_handler(served))
        server.daemon_threads = True
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        base = f"http://127.0.0.1:{server.server_port}"
        try:
            for path, expected in (("/healthz", b"OK\n"),
                                   ("/proof.txt", repo.PROOF),
                                   (f"/{arch}/APKINDEX.tar.gz", index),
                                   (f"/{arch}/tree-2.2.1-r0.apk", package)):
                assert urllib.request.urlopen(base + path, timeout=2).read() == expected
            for path in ("/repositories", "/index-path", "/../proof.txt",
                         "/%2e%2e/proof.txt", "/riscv64/missing.apk"):
                try:
                    urllib.request.urlopen(base + path, timeout=2)
                except urllib.error.HTTPError as exc:
                    assert exc.code == 404
                else:
                    raise AssertionError("unsafe path was served: " + path)
        finally:
            server.shutdown()
            server.server_close()
            worker.join(timeout=2)

        repo.write_atomic(root / "proof.txt", b"tampered\n")
        try:
            http.load_repository(root)
        except ValueError:
            pass
        else:
            raise AssertionError("changed proof bytes accepted")
    print("[M195 HOST REPOSITORY CONTRACT PASS] signed container, metadata, "
          "SHA256 manifest, localhost HTTP and path rejection")


if __name__ == "__main__":
    check()
