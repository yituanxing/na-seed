#!/usr/bin/env python3
"""Stage an unchanged, signed Alpine v3.23 repository for native QEMU.

The host is only a transport/cache. It neither rebuilds packages nor signs an
index. The original guest apk must still authenticate the signed APKINDEX and
package against the target Alpine keyring before installing anything.
"""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import tarfile
import tempfile
import urllib.error
import urllib.request
import zlib


ARCH = {"amd64": "x86_64", "riscv64": "riscv64", "arm64": "aarch64"}
VERSION = "2.2.1-r0"
PACKAGE = "tree"
RELEASE = "v3.23"
ORIGIN = "https://dl-cdn.alpinelinux.org/alpine"
INDEX_LIMIT = 24 * 1024 * 1024
APK_LIMIT = 4 * 1024 * 1024
EXPANDED_LIMIT = 128 * 1024 * 1024
FILE_RE = re.compile(r"^(?:x86_64|riscv64|aarch64)/(?:APKINDEX\.tar\.gz|tree-2\.2\.1-r0\.apk)$|^(?:proof\.txt|repositories|index-path|package-path)$")
PROOF = b"M195_SIGNED_ALPINE_WIRE_2026\n"


def fail(message: str) -> None:
    raise ValueError("[M195 REPOSITORY FAIL] " + message)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fetch_official(url: str, limit: int) -> bytes:
    if not url.startswith(ORIGIN + "/" ):
        fail("non-official source URL")
    request = urllib.request.Request(url, headers={"User-Agent": "NaSeed-M195/1"})
    with urllib.request.urlopen(request, timeout=90) as reply:
        # A CDN may redirect, but it must not downgrade signed APK transport.
        if not reply.geturl().startswith("https://"):
            fail("insecure repository redirect")
        data = bytearray()
        while True:
            chunk = reply.read(min(1024 * 1024, limit + 1 - len(data)))
            if not chunk:
                break
            data.extend(chunk)
            if len(data) > limit:
                fail("official payload exceeds bounded size")
    if not data:
        fail("empty official download")
    return bytes(data)


def gzip_members(payload: bytes) -> list[dict[str, bytes]]:
    """Read the separate signed gzip/tar streams in Alpine APK v2 objects."""
    remaining = payload
    result = []
    total = 0
    while remaining:
        stream = zlib.decompressobj(16 + zlib.MAX_WBITS)
        expanded = stream.decompress(remaining, EXPANDED_LIMIT + 1 - total)
        if not stream.eof or stream.unconsumed_tail:
            fail("invalid or oversized signed gzip segment")
        total += len(expanded)
        if total > EXPANDED_LIMIT:
            fail("signed archive decompression limit exceeded")
        try:
            with tarfile.open(fileobj=io.BytesIO(expanded), mode="r:") as archive:
                members = {}
                for item in archive.getmembers():
                    if not item.isfile() or item.size > EXPANDED_LIMIT:
                        continue
                    member = archive.extractfile(item)
                    if member is None:
                        continue
                    # The archive is never extracted to a filesystem.
                    members[item.name.removeprefix("./")] = member.read()
                result.append(members)
        except (tarfile.TarError, OSError) as exc:
            fail("malformed signed tar segment: " + str(exc))
        remaining = stream.unused_data
        if len(result) > 5:
            fail("too many package archive segments")
    if not result:
        fail("no signed gzip segments")
    return result


def signed_members(payload: bytes) -> list[dict[str, bytes]]:
    members = gzip_members(payload)
    if not any(name.startswith(".SIGN.") for name in members[0]):
        fail("upstream object has no APK signature segment")
    return members


def index_record(payload: bytes, arch: str) -> dict[str, str]:
    segments = signed_members(payload)
    indexes = [segment["APKINDEX"] for segment in segments if "APKINDEX" in segment]
    if len(indexes) != 1:
        fail("signed repository has no unique APKINDEX")
    try:
        records = indexes[0].decode("utf-8").split("\n\n")
    except UnicodeDecodeError:
        fail("APKINDEX is not UTF-8")
    matches = []
    for record in records:
        fields = {}
        for line in record.splitlines():
            if len(line) >= 3 and line[1] == ":":
                fields[line[0]] = line[2:]
        if fields.get("P") == PACKAGE:
            matches.append(fields)
    if len(matches) != 1:
        fail("signed index does not contain exactly one tree package")
    result = matches[0]
    if result.get("V") != VERSION:
        fail("unexpected tree version: " + result.get("V", "missing"))
    if result.get("A") not in (None, arch):
        fail("tree package architecture does not match repository")
    if not result.get("C"):
        fail("tree package has no signed index checksum")
    return result


def validate_package(payload: bytes, arch: str) -> None:
    segments = signed_members(payload)
    pkginfo = [segment[".PKGINFO"] for segment in segments if ".PKGINFO" in segment]
    if len(pkginfo) != 1:
        fail("signed package has no unique .PKGINFO")
    try:
        text = pkginfo[0].decode("utf-8")
    except UnicodeDecodeError:
        fail("package metadata is not UTF-8")
    fields = {}
    for line in text.splitlines():
        if " = " in line:
            key, value = line.split(" = ", 1)
            fields.setdefault(key, []).append(value)
    for key, expected in (("pkgname", PACKAGE), ("pkgver", VERSION), ("arch", arch)):
        if fields.get(key) != [expected]:
            fail("package metadata mismatch for " + key)


def manifest(port: str, index: bytes, package: bytes) -> dict[str, object]:
    arch = ARCH[port]
    filename = f"{PACKAGE}-{VERSION}.apk"
    # apk appends /$arch/APKINDEX.tar.gz and /$arch/name-version.apk to
    # this repository BASE URL (both its v2 and v3 repository layouts).
    repositories = b"http://10.0.2.2:15417\n"
    index_path = f"{arch}/APKINDEX.tar.gz\n".encode()
    package_path = f"{arch}/{filename}\n".encode()
    return {
        "schema": 1, "release": RELEASE, "port": port, "arch": arch,
        "package": PACKAGE, "version": VERSION, "origin": ORIGIN,
        "files": [
            {"path": f"{arch}/APKINDEX.tar.gz", "size": len(index), "sha256": sha256(index)},
            {"path": f"{arch}/{filename}", "size": len(package), "sha256": sha256(package)},
            {"path": "proof.txt", "size": len(PROOF), "sha256": sha256(PROOF)},
            {"path": "repositories", "size": len(repositories), "sha256": sha256(repositories)},
            {"path": "index-path", "size": len(index_path), "sha256": sha256(index_path)},
            {"path": "package-path", "size": len(package_path), "sha256": sha256(package_path)},
        ],
    }


def write_atomic(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink():
        fail("refusing symbolic link output: " + str(path))
    with tempfile.NamedTemporaryFile(prefix=".m195-", dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    os.replace(temporary, path)


def check_tree(root: Path, port: str) -> dict[str, object]:
    arch = ARCH[port]
    info_path = root / f"m195-{arch}.json"
    if info_path.is_symlink() or not info_path.is_file():
        fail("missing safe manifest")
    info = json.loads(info_path.read_text(encoding="utf-8"))
    if (info.get("schema") != 1 or info.get("port") != port or
            info.get("arch") != arch or info.get("package") != PACKAGE or
            info.get("version") != VERSION or info.get("release") != RELEASE):
        fail("manifest identity mismatch")
    files = info.get("files")
    if not isinstance(files, list) or len(files) != 6:
        fail("manifest must contain signed objects and fixed host fixtures")
    expected = {f"{arch}/APKINDEX.tar.gz", f"{arch}/{PACKAGE}-{VERSION}.apk", "proof.txt", "repositories", "index-path", "package-path"}
    if {entry.get("path") for entry in files} != expected:
        fail("unexpected manifest paths")
    for entry in files:
        name = entry["path"]
        if not FILE_RE.fullmatch(name):
            fail("unsafe repository path")
        path = root / name
        if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(root.resolve()):
            fail("unsafe repository member")
        data = path.read_bytes()
        if len(data) != entry.get("size") or sha256(data) != entry.get("sha256"):
            fail("repository member changed after verified staging: " + name)
    return info


def stage(port: str, root: Path) -> dict[str, object]:
    arch = ARCH[port]
    base = f"{ORIGIN}/{RELEASE}/main/{arch}"
    index = fetch_official(base + "/APKINDEX.tar.gz", INDEX_LIMIT)
    index_record(index, arch)
    package = fetch_official(base + f"/{PACKAGE}-{VERSION}.apk", APK_LIMIT)
    validate_package(package, arch)
    info = manifest(port, index, package)
    write_atomic(root / arch / "APKINDEX.tar.gz", index)
    write_atomic(root / arch / f"{PACKAGE}-{VERSION}.apk", package)
    write_atomic(root / "proof.txt", PROOF)
    write_atomic(root / "repositories", b"http://10.0.2.2:15417\n")
    write_atomic(root / "index-path", f"{arch}/APKINDEX.tar.gz\n".encode())
    write_atomic(root / "package-path", f"{arch}/{PACKAGE}-{VERSION}.apk\n".encode())
    write_atomic(root / f"m195-{arch}.json", (json.dumps(info, sort_keys=True, indent=2) + "\n").encode())
    check_tree(root, port)
    return info


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("arguments", nargs="+", metavar="ARG")
    args = parser.parse_args()
    arguments = args.arguments
    if len(arguments) == 2:
        action = "stage"
        port, root_arg = arguments
    elif len(arguments) == 3:
        action, port, root_arg = arguments
    else:
        parser.error("usage: m195-alpine-repository.py [stage|verify] amd64|riscv64|arm64 OUTDIR")
    if action not in ("stage", "verify") or port not in ARCH:
        parser.error("invalid action or Alpine target")
    root = Path(root_arg)
    try:
        info = stage(port, root) if action == "stage" else check_tree(root, port)
    except (ValueError, OSError, urllib.error.URLError, json.JSONDecodeError) as exc:
        parser.exit(1, str(exc) + "\n")
    print(f"[M195 SIGNED ALPINE REPOSITORY PASS] port={info['port']} arch={info['arch']} "
          f"package={PACKAGE}-{VERSION} verified_files=6", flush=True)


if __name__ == "__main__":
    main()
