#!/usr/bin/env python3
"""Exercise the actual installer with isolated firmware, tools and apt logs."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/ci-install-signed-alpine-qemu-deps.sh"
COMMON = ("clang", "ld.lld", "readelf", "cpio", "xorriso", "mformat",
          "sgdisk", "qemu-riscv64", "docker")
COHORT = ("cmake", "g++", "mke2fs", "debugfs", "e2fsck", "curl", "python3", "openssl")
QEMU = {"amd64": "qemu-system-x86_64", "riscv64": "qemu-system-riscv64",
        "arm64": "qemu-system-aarch64"}
FW = {"amd64": "/usr/share/OVMF/OVMF_CODE_4M.fd",
      "riscv64": "/usr/share/qemu-efi-riscv64/RISCV_VIRT_CODE.fd",
      "arm64": "/usr/share/AAVMF/AAVMF_CODE.fd"}
ARM_FALLBACK = "/usr/share/qemu-efi-aarch64/QEMU_EFI.fd"
IPXE = "/usr/lib/ipxe/qemu/efi-virtio.rom"

def check(port, *, present=(), absent=(), firmware=None, extra=(),
          fallback=False, legacy=False, cross=None, explicit=False,
          expected=(), expect_failure=False, cohort=False, docker_error=None):
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        bins = root / "bin"
        bins.mkdir()
        for tool in set(COMMON + (QEMU.get(port, "unused"),) + tuple(present) + (COHORT if cohort else ())) - set(absent):
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
        if legacy:
            args.append("--arm64-efi-legacy")
        if cross:
            args.append('--cross-' + cross)
        if explicit:
            args.append('--explicit-tools')
        if cohort:
            args.append("--cohort-tools")
            if docker_error is not None:
                args.append(docker_error)
        args.extend(extra)
        result = subprocess.run(args, cwd=root, env={"PATH": str(bins),
                                "APT_LOG": str(log)}, capture_output=True, text=True)
        assert (result.returncode != 0) == expect_failure, (args, result.stderr)
        if cohort:
            assert not result.stdout, (args, result.stdout)
            if docker_error is not None:
                assert result.returncode == 2 and result.stderr.strip() == docker_error
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
check("amd64", fallback=True, cross="cxx")
for port, arch, pkg in (("riscv64", "riscv64", "riscv64"),
                        ("arm64", "aarch64", "arm64")):
    gcc = arch + "-linux-gnu-gcc"
    gxx = arch + "-linux-gnu-g++"
    headers = "/usr/" + arch + "-linux-gnu/include/bits/wordsize.h"
    libc = "/usr/" + arch + "-linux-gnu/lib/libc.a"
    check(port, fallback=True, cross="c",
          expected=("gcc-" + arch + "-linux-gnu", "libc6-dev-" + pkg + "-cross"))
    check(port, fallback=True, cross="cxx", present=(gcc,),
          firmware=(FW[port], headers),
          expected=("g++-" + arch + "-linux-gnu", "libc6-dev-" + pkg + "-cross"))
    check(port, fallback=True, cross="cxx", present=(gcc, gxx),
          firmware=(FW[port], headers, libc))
    check(port, fallback=True, cross="c", present=(gcc,),
          firmware=(FW[port], headers, libc))
# Older x86 native-only groups must not gain qemu-user or unrelated tools.
check("amd64", explicit=True, absent=("clang", "qemu-riscv64", "cpio"),
      extra=("clang:clang", "debugfs:e2fsprogs"),
      expected=("clang", "e2fsprogs"))
check("arm64", explicit=True, legacy=True, firmware=(ARM_FALLBACK,),
      absent=("qemu-riscv64", "clang"))
check("arm64", explicit=True, legacy=True, firmware=(FW["arm64"],),
      expected=("qemu-efi-aarch64",))
check("arm64", explicit=True, fallback=True, cross="cxx", firmware=(ARM_FALLBACK,),
      extra=("openssl:openssl",),
      expected=("openssl", "gcc-aarch64-linux-gnu", "g++-aarch64-linux-gnu",
                "libc6-dev-arm64-cross"))
# Full native signed APK cohort needs libc.a but deliberately not headers.
check("amd64", cohort=True, absent=("qemu-riscv64",))
check("amd64", cohort=True, absent=("cmake", "e2fsck", "openssl"),
      expected=("cmake", "e2fsprogs", "openssl"))
check("amd64", cohort=True, absent=("docker",), expect_failure=True)
check("amd64", cohort=True, absent=("docker",), expect_failure=True,
      docker_error="native signed APK inventory requires Docker")
for port, arch, pkg in (("riscv64", "riscv64", "riscv64"),
                        ("arm64", "aarch64", "arm64")):
    gcc, gxx = arch + "-linux-gnu-gcc", arch + "-linux-gnu-g++"
    libc = "/usr/" + arch + "-linux-gnu/lib/libc.a"
    firmware = ARM_FALLBACK if port == "arm64" else FW[port]
    check(port, cohort=True, present=(gcc, gxx), firmware=(firmware, libc))
    check(port, cohort=True, present=(gcc,), firmware=(firmware,),
          expected=("g++-" + arch + "-linux-gnu", "libc6-dev-" + pkg + "-cross"))
print("[CI DEPS PASS] three ISAs, firmware alternatives, extra packages, failure paths")
