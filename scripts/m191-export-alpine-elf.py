#!/usr/bin/env python3
"""Export exact original Alpine BusyBox/musl bytes for the rescue CPIO.

Alpine may use guest-absolute symlinks such as /bin -> /usr/bin.  Host
readlink -f and ordinary shell `cp root/bin/busybox` can escape into the
host system; resolve every component against the guest root instead.
The independent physical ext4 image retains the signed package's files.
"""

from pathlib import Path
import runpy
import shutil
import sys


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit("usage: m191-export-alpine-elf.py PORT ROOT OUT")
    port, root_text, out_text = sys.argv[1:]
    alpine_arch = {"amd64": "x86_64", "riscv64": "riscv64", "arm64": "aarch64"}.get(port)
    if alpine_arch is None:
        raise SystemExit("[M191 ELF EXPORT FAIL] unsupported target: " + port)
    root = Path(root_text).resolve(strict=True)
    out = Path(out_text).resolve()
    model = runpy.run_path("scripts/m191-alpine-root-disk.py")
    model["validate_tree"](port, root)
    out.mkdir(parents=True, exist_ok=True)
    for guest, output in (
        ("/bin/busybox", "busybox"),
        ("/lib/ld-musl-" + alpine_arch + ".so.1", "ld-musl-" + alpine_arch + ".so.1"),
    ):
        source = model["guest_path"](root, guest)
        if not source.is_file() or source.stat().st_size < 64:
            raise ValueError("[M191 ELF EXPORT FAIL] missing original guest ELF: " + guest)
        # No symlink/host-path identity is inferred from file spelling.
        source.relative_to(root)
        with source.open("rb") as handle:
            if handle.read(4) != b"\x7fELF":
                raise ValueError("[M191 ELF EXPORT FAIL] invalid source ELF: " + guest)
        destination = out / output
        shutil.copyfile(source, destination)
        destination.chmod(0o755)
        if source.read_bytes() != destination.read_bytes():
            raise ValueError("[M191 ELF EXPORT FAIL] copied ELF differs: " + guest)
        print("[M191 ELF EXPORT] exact original Alpine guest " + guest)


if __name__ == "__main__":
    main()
