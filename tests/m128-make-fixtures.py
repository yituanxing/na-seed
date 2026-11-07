#!/usr/bin/env python3
"""Host-side stable file tree, USTAR stream, and raw blank exFAT image.

Only these INPUTS are constructed on the host. The 15 unmodified Alpine
programs perform actual image generation, validation, extraction and mutation
inside NaSeed, across three real native QEMU targets.
"""
import io
import pathlib
import sys
import tarfile

assert len(sys.argv) == 2
root = pathlib.Path(sys.argv[1])
tree = root / "m128-tree"
(tree / "nested").mkdir(parents=True, exist_ok=True)
contents = {
    "alpha.txt": b"M128_SQUASH_CROSS_IMPL_ALPHA\n",
    "nested/beta.txt": b"M128_SQUASH_CROSS_IMPL_BETA\n",
}
for name, data in contents.items():
    (tree / name).write_bytes(data)
with tarfile.open(root / "m128-input.tar", "w", format=tarfile.USTAR_FORMAT) as arc:
    for name, mode in (("nested", 0o755),):
        info = tarfile.TarInfo(name)
        info.type = tarfile.DIRTYPE
        info.mode = mode
        info.mtime = 0
        info.uid = info.gid = 0
        arc.addfile(info)
    for name, data in contents.items():
        info = tarfile.TarInfo(name)
        info.size = len(data)
        info.mode = 0o644
        info.mtime = 0
        info.uid = info.gid = 0
        arc.addfile(info, io.BytesIO(data))
with (root / "m128-blank-exfat.img").open("wb") as blank:
    blank.truncate(16 * 1024 * 1024)
print("[M128 FIXTURES PASS] 2 fixed data files, deterministic tar, 16MiB exFAT blank")
