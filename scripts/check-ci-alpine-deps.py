#!/usr/bin/env python3
"""Exercise the actual installer with isolated firmware, tools and apt logs."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/ci-install-signed-alpine-qemu-deps.sh"
COMMON = ("clang", "ld.lld", "readelf", "cpio", "xorriso", "mformat",
          "sgdisk", "qemu-riscv64", "docker")
QEMU = {"amd64": "qemu-system-x86_64", "riscv64": "qemu-system-riscv64",
        "arm64": "qemu-system-aarch64"}
FW = {"amd64": "/usr/share/OVMF/OVMF_CODE_4M.fd",
      "riscv64": "/usr/share/qemu-efi-riscv64/RISCV_VIRT_CODE.fd",
      "arm64": "/usr/share/AAVMF/AAVMF_CODE.fd"}
ARM_FALLBACK = "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd"
IPXE = "/usr/lib/ipxe/qemu/efi-virtio.rom"

def check(port, *, present=(), absent=(), firmware=None, extra=(),
          fallback=False, expected=(), expect_failure=False):
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        bins = root / "bin"
        bins.mkdir()
        for tool in set(COMMON + (QEMU.get(port, "unused"),) + tuple(present)) - set(absent):
            (bins / tool).symlink_to("/bin/true")
        (bins / "sh").symlink_to("/bin/sh")
        log = root / "apt.log"
        sudo = bins / "sudo"
        sudo.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$APT_LOG"\n')
        sudo.chmod(0o755)
        (root / "scripts").mkdir()
        (root / "scripts/ci-ubuntu-mirror.sh").write_text("exit 0\n")
        for path in ((FW.get(port),) if firmware is None else firmware):
            if path:
                f = root / path.lstrip("/")
                f.parent.mkdir(parents=True, exist_ok=True)
                f.touch()
        if port in ("riscv64", "arm64"):
            f = root / IPXE.lstrip("/")
            f.parent.mkdir(parents=True, exist_ok=True)
            f.touch()
        # Only redirect absolute firmware paths. All decisions, shell
        # argument expansion and package selection come from production code.
        copy = root / "installer.sh"
        copy.write_text(SCRIPT.read_text().replace("/usr/", str(root) + "/usr/"))
        args = ["/bin/sh", str(copy), port]
        if fallback:
            args.append("--arm64-efi-fallback")
        args.extend(extra)
        result = subprocess.run(args, cwd=root, env={"PATH": str(bins),
                                "APT_LOG": str(log)}, capture_output=True, text=True)
        assert (result.returncode != 0) == expect_failure, (args, result.stderr)
        lines = log.read_text().splitlines() if log.exists() else []
        if not expected:
            assert not lines, (args, lines)
        else:
            assert len(lines) == 2 and " update " in lines[0], (args, lines)
            assert " --no-install-recommends " in lines[1], lines
            packages = lines[1].split(" --no-install-recommends ", 1)[1].split()
            assert sorted(packages) == sorted(expected), (args, packages, expected)

subprocess.run(["/bin/sh", "-n", str(SCRIPT)], check=True)
for port in QEMU:
    check(port)
check("amd64", absent=("clang",), firmware=(), extra=("mke2fs:e2fsprogs",),
      expected=("clang", "ovmf", "e2fsprogs"))
check("riscv64", absent=("qemu-system-riscv64",), firmware=(),
      expected=("qemu-system-misc", "qemu-efi-riscv64"))
check("arm64", firmware=(ARM_FALLBACK,), fallback=True)
check("arm64", firmware=(ARM_FALLBACK,), expected=("qemu-efi-aarch64",))
check("arm64", firmware=(), fallback=True, expected=("qemu-efi-aarch64",))
check("arm64", fallback=True, extra=("mke2fs:e2fsprogs", "openssl:openssl"),
      present=("mke2fs",), expected=("openssl",))
check("amd64", absent=("docker",), expect_failure=True)
check("invalid", expect_failure=True)
print("[CI DEPS PASS] three ISAs, firmware alternatives, extra packages, failure paths")
