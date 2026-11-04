#!/usr/bin/env python3
"""Read-only forensic match for M194's real RV64 Alpine DSO callsite.

The QEMU exception supplied an RX mmap span and return PC, not a pathname.
Compare that span against actual *unmodified* Alpine ELF PT_LOAD segments;
never silently label a guessed library a proven match. This diagnostic is
not a guest acceptance oracle and does not edit or execute foreign ELF files.
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys


FAULT_MAP_SPAN = 0x33F000
FAULT_CALLSITE_DELTA = 0x5FD361A2 - 0x5FB2E000
FAULT_CALLBACK_DELTA = 0x5FED0420 - 0x5FB2E000
PAGE = 4096
ELF_HEADER = struct.Struct("<16sHHIQQQIHHHHHH")
PROGRAM_HEADER = struct.Struct("<IIQQQQQQ")
SYM = re.compile(r"^\s*\d+:\s+([0-9a-fA-F]+)\s+(\d+)\s+"
                 r"(FUNC|IFUNC)\s+\S+\s+\S+\s+\S+\s+(.+)$")
OBJECT_SYM = re.compile(r"^\s*\d+:\s+([0-9a-fA-F]+)\s+(\d+)\s+"
                        r"OBJECT\s+\S+\s+\S+\s+\S+\s+(.+)$")
SECTION = re.compile(r"^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+"
                     r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+")
RELOCATION = re.compile(r"^\s*([0-9a-fA-F]{8,16})\s+[0-9a-fA-F]+\s+"
                        r"R_RISCV_\S+\s*.*$")


def align_down(n: int) -> int:
    return n & ~(PAGE - 1)


def align_up(n: int) -> int:
    return (n + PAGE - 1) & ~(PAGE - 1)


def executable_loads(path: Path) -> list[tuple[int, int, int, int]]:
    """(mapped span, file offset, ELF virtual start, byte length)."""
    try:
        with path.open("rb") as stream:
            header = stream.read(ELF_HEADER.size)
            if len(header) != ELF_HEADER.size:
                return []
            values = ELF_HEADER.unpack(header)
            ident, machine, phoff = values[0], values[2], values[5]
            phentsize, phnum = values[9], values[10]
            if ident[:7] != b"\x7fELF\x02\x01\x01" or machine != 243:
                return []
            if phentsize != PROGRAM_HEADER.size or not 0 < phnum <= 128:
                return []
            if phoff + phnum * phentsize > path.stat().st_size:
                return []
            stream.seek(phoff)
            loads = []
            for _ in range(phnum):
                raw = stream.read(phentsize)
                ptype, flags, offset, vaddr, _, filesz, memsz, _ = PROGRAM_HEADER.unpack(raw)
                if ptype != 1 or not (flags & 1) or not memsz:
                    continue
                mapped_start = align_down(vaddr)
                mapped_end = align_up(vaddr + memsz)
                loads.append((mapped_end - mapped_start, offset, vaddr, filesz))
            return loads
    except (OSError, OverflowError, struct.error):
        return []


def elf_paths(root: Path):
    seen: set[tuple[int, int]] = set()
    for dirname in ("lib", "usr/lib", "usr/local/lib", "usr/libexec"):
        base = root / dirname
        if not base.is_dir() or base.is_symlink():
            continue
        for here, _, files in os.walk(base, followlinks=False):
            for name in files:
                path = Path(here) / name
                if path.is_symlink():
                    continue
                try:
                    stat = path.stat()
                except OSError:
                    continue
                key = (stat.st_dev, stat.st_ino)
                if key in seen or stat.st_size < 64:
                    continue
                seen.add(key)
                yield path


def readelf(path: Path, option: str) -> str:
    if shutil.which("readelf") is None:
        return "[M194 RV64 DSO] host readelf unavailable"
    try:
        result = subprocess.run(("readelf", option, str(path)), text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=12, check=False)
        return result.stdout if result.returncode == 0 else (
            "[M194 RV64 DSO] readelf failed: " + result.stdout[-1200:])
    except (OSError, subprocess.TimeoutExpired) as exc:
        return f"[M194 RV64 DSO] readelf unavailable: {exc}"


def symbol_context(path: Path, virtual_pc: int) -> None:
    symbols: list[tuple[int, int, str]] = []
    for line in readelf(path, "-Ws").splitlines():
        found = SYM.match(line)
        if not found:
            continue
        value = int(found[1], 16)
        if value:
            symbols.append((value, int(found[2]), found[4].strip()))
    if not symbols:
        print("[M194 RV64 DSO] no public function symbols near native callsite")
        return
    symbols.sort(key=lambda x: x[0])
    before = [sym for sym in symbols if sym[0] <= virtual_pc][-4:]
    after = [sym for sym in symbols if sym[0] > virtual_pc][:4]
    for va, size, name in before + after:
        print(f"[M194 RV64 DSO SYMBOL] va=0x{va:x} size=0x{size:x} "
              f"distance={virtual_pc-va:+d} name={name}")


def callsite_bytes(path: Path, segment: tuple[int, int, int, int]) -> None:
    _, offset, vaddr, filesz = segment
    virtual_pc = align_down(vaddr) + FAULT_CALLSITE_DELTA
    if not vaddr <= virtual_pc < vaddr + filesz:
        print("[M194 RV64 DSO] callsite is not inside candidate file-backed RX bytes")
        return
    file_pc = offset + virtual_pc - vaddr
    first, count = max(0, file_pc - 48), 64
    with path.open("rb") as stream:
        stream.seek(first)
        data = stream.read(count)
    print(f"[M194 RV64 DSO CALLSITE] elf_va=0x{virtual_pc:x} "
          f"file_offset=0x{file_pc:x} preview_start=0x{first:x} "
          f"hex={data.hex()}")
    symbol_context(path, virtual_pc)


def callback_data(path: Path) -> None:
    """Inspect *original* ELF callback slot; never infer guest writes from it."""
    callback_va = FAULT_CALLBACK_DELTA
    phdrs: list[tuple[int, int, int, int, int]] = []
    try:
        with path.open("rb") as stream:
            fields = ELF_HEADER.unpack(stream.read(ELF_HEADER.size))
            phoff, phentsize, phnum = fields[5], fields[9], fields[10]
            if phentsize != PROGRAM_HEADER.size or not 0 < phnum <= 128:
                return
            for k in range(phnum):
                stream.seek(phoff + k * phentsize)
                ptype, flags, offset, vaddr, _, filesz, memsz, _ = (
                    PROGRAM_HEADER.unpack(stream.read(phentsize)))
                if ptype == 1 and vaddr <= callback_va < vaddr + memsz:
                    phdrs.append((flags, offset, vaddr, filesz, memsz))
            if not phdrs:
                print(f"[M194 RV64 DSO CALLBACK] va=0x{callback_va:x} "
                      "NO FILE PT_LOAD MATCH")
                return
            flags, offset, vaddr, filesz, memsz = phdrs[0]
            file_offset = offset + callback_va - vaddr
            if callback_va - vaddr >= filesz:
                print(f"[M194 RV64 DSO CALLBACK] va=0x{callback_va:x} "
                      f"in zero-filled PT_LOAD flags=0x{flags:x}")
                return
            window_start = max(offset, file_offset - 32)
            window_size = min(96, offset + filesz - window_start)
            stream.seek(window_start)
            original = stream.read(window_size)
            stream.seek(file_offset)
            qword = stream.read(8)
    except (OSError, struct.error, OverflowError) as exc:
        print(f"[M194 RV64 DSO CALLBACK] original file read failed: {exc}")
        return
    print(f"[M194 RV64 DSO CALLBACK] elf_va=0x{callback_va:x} "
          f"file_offset=0x{file_offset:x} phdr_flags=0x{flags:x} "
          f"file_qword=0x{int.from_bytes(qword, 'little'):016x} "
          f"preview_start=0x{window_start:x} hex={original.hex()}")
    for line in readelf(path, "-SW").splitlines():
        found = SECTION.match(line)
        if found:
            name, addr, _, size = found.groups()
            start, length = int(addr, 16), int(size, 16)
            if start <= callback_va < start + length:
                print(f"[M194 RV64 DSO CALLBACK SECTION] {name} "
                      f"start=0x{start:x} size=0x{length:x} "
                      f"offset_in_section=0x{callback_va-start:x}")
    objects: list[tuple[int, int, str]] = []
    for line in readelf(path, "-Ws").splitlines():
        found = OBJECT_SYM.match(line)
        if found and int(found[1], 16):
            objects.append((int(found[1], 16), int(found[2]), found[3].strip()))
    if objects:
        nearby = sorted(objects, key=lambda entry: abs(entry[0] - callback_va))[:6]
        for address, size, name in nearby:
            print(f"[M194 RV64 DSO CALLBACK OBJECT] va=0x{address:x} "
                  f"size=0x{size:x} delta={address-callback_va:+d} name={name}")
    matches: list[tuple[int, str]] = []
    for line in readelf(path, "-rW").splitlines():
        found = RELOCATION.match(line)
        if found:
            reloc_va = int(found[1], 16)
            if abs(reloc_va - callback_va) <= 64:
                matches.append((reloc_va, line.strip()))
    for addr, line in sorted(matches, key=lambda item: abs(item[0]-callback_va))[:24]:
        print(f"[M194 RV64 DSO CALLBACK RELOC] delta={addr-callback_va:+d} {line}")
    if not matches:
        print("[M194 RV64 DSO CALLBACK RELOC] none within +-64 bytes")


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: m194-rv64-apk-dso-audit.py ALPINE_ROOTFS")
    root = Path(sys.argv[1])
    if not root.is_dir() or root.is_symlink():
        raise SystemExit("[M194 RV64 DSO] staged original rootfs missing")

    matches = []
    nearest = []
    checked = 0
    for path in elf_paths(root):
        for segment in executable_loads(path):
            checked += 1
            distance = abs(segment[0] - FAULT_MAP_SPAN)
            candidate = (distance, path, segment)
            nearest.append(candidate)
            if distance == 0:
                matches.append(candidate)
    print(f"[M194 RV64 DSO] scanned={checked} original ELF RX LOAD segments "
          f"actual_guest_rx_span=0x{FAULT_MAP_SPAN:x} "
          f"callsite_offset=0x{FAULT_CALLSITE_DELTA:x}")
    if not matches:
        print("[M194 RV64 DSO] NO EXACT RX SPAN MATCH: pathname remains UNPROVEN")
    for distance, path, segment in sorted(matches or nearest,
                                          key=lambda x: (x[0], str(x[1])))[:5]:
        shown = path.relative_to(root)
        print(f"[M194 RV64 DSO CANDIDATE] {shown} "
              f"distance=0x{distance:x} rx_span=0x{segment[0]:x} "
              f"sha256={hashlib.sha256(path.read_bytes()).hexdigest()}")
        callsite_bytes(path, segment)
        callback_data(path)
        # Print ELF program headers for mapping verification independently of
        # any target-toolchain-specific objdump or disassembler installation.
        print("[M194 RV64 DSO PHDR] " + "\n".join(
            readelf(path, "-lW").splitlines()[:46]))


if __name__ == "__main__":
    main()
