#!/usr/bin/env python3
"""M50: verify the actual linked x86 and both full-kernel source sets.

The historical manifest guarded only RV64/AArch64. A common kernel TU
omitted from the x86 linker would otherwise pass shared cross-ISA audits.
"""
from pathlib import Path
import re
root=Path(__file__).resolve().parent.parent
srcs=[line.strip() for line in (root/"scripts/full-kernel-core.sources").read_text().splitlines()
      if line.strip() and not line.startswith("#")]
assert len(srcs)==len(set(srcs))==39, "common kernel TU manifest drift"
make=(root/"Makefile").read_text()
defs={}
for line in make.splitlines():
    m=re.match(r"^\$\(OUT\)/([a-z0-9_]+\.o):\s+(kernel/[\w/]+\.c)(?:\s|$)",line)
    if m:
        if m.group(2) in defs:
            raise SystemExit(f"duplicate x86 source object mapping: {m.group(2)}")
        defs[m.group(2)]=m.group(1)
linked=re.search(r"^\$\(KERNEL\):\s+([^\n]+)",make,re.M)
if not linked:
    raise SystemExit("x86 kernel has no object linkage dependency rule")
actual_link=re.search(r"^\s*ld\.lld \$\(LDFLAGS\)\s+([^\n]+)",make,re.M)
if not actual_link:
    raise SystemExit("x86 kernel has no actual ld.lld link recipe")
for source in srcs:
    if not (root/source).is_file():
        raise SystemExit(f"missing shared source: {source}")
    obj=defs.get(source)
    if obj is None:
        raise SystemExit(f"x86 kernel missing exact common compilation rule: {source}")
    if f"$(OUT)/{obj}" not in linked.group(1).split():
        raise SystemExit(f"x86 kernel linker omits common object {obj} ({source})")
    if f"$(OUT)/{obj}" not in actual_link.group(1).split():
        raise SystemExit(f"x86 actual ld.lld command omits common object {obj} ({source})")
for port in ("aarch64","riscv64"):
    script=(root/f"scripts/build-{port}-full.sh").read_text()
    if 'scripts/full-kernel-core.sources' not in script or 'for f in $common $arch' not in script:
        raise SystemExit(f"native {port} bypassed shared kernel TU manifest")
    if "kernel/arch/" not in script:
        raise SystemExit(f"native {port} missing actual CPU/MMU providers")
# Ensure syscall-ABI owners remain separate from the historical FS dispatcher.
fs_dispatch = (root/"kernel/fs/syscall.c").read_text()
for path, symbol in (("kernel/fs/namespace_syscall.c", "nas_fs_resolve_path"),
                     ("kernel/fs/socket_syscall.c", "sys_unix_sendmsg")):
    if symbol not in (root/path).read_text():
        raise SystemExit(f"lost dedicated FS owner {path}: {symbol}")
    import re
    if re.search(r"^(?:static\s+)?(?:uint64_t|int64_t|int|bool)\s+"
                 + re.escape(symbol) + r"\s*\(", fs_dispatch, re.M):
        raise SystemExit(f"FS syscall owner definition leaked back into monolith: {symbol}")
# One actual FD entry per descriptor: reject parallel arrays reappearing.
task_h=(root/"kernel/task.h").read_text()
fd_h=(root/"kernel/fd.h").read_text()
assert "struct nas_fd_entry entries[NAS_FD_LIMIT]" in task_h
for old in ("descriptors[NAS_FD_LIMIT]", "file_slot[NAS_FD_LIMIT]", "fd_cloexec[NAS_FD_LIMIT]"):
    assert old not in task_h, f"parallel descriptor storage returned: {old}"
for name in ("kind", "slot", "cloexec"):
    assert f"uint8_t {name};" in fd_h
for source in (root/"kernel").rglob("*"):
    if source.suffix not in (".c", ".h"): continue
    data=source.read_text()
    for old in ("descriptors[", "file_slot[", "fd_cloexec["):
        if old in data: raise SystemExit(f"old FD storage escaped in {source}: {old}")
print(f"[M50 ARCH PASS] all {len(srcs)} actual shared kernel TUs are linked on x86 "
      "and built on RV64/AArch64; hardware providers remain ISA-specific")
