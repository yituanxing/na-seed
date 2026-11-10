#!/usr/bin/env python3
"""Offline guardrail tests; never substitute for native initctl QEMU evidence."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name: str, module: str):
    spec = importlib.util.spec_from_file_location(module, ROOT / "scripts" / name)
    assert spec and spec.loader
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


prov = load("m256-debian-provenance.py", "m256_signed_telinit_test")
disk = load("m256-debian-ext4-disk.py", "m256_disk_test")
uart = load("m256-debian-uart.py", "m256_uart_test")


def fake():
    m255 = uart.m255
    return b"\n".join((
        *m255.REQUIRED_ORDER[:3],
        *uart.RUN_MARKERS[:2],
        m255.REQUIRED_ORDER[3],
        b"M255-SERVICE-FIRST-PID=8",
        b"M255-SERVICE-RESPAWN-PID=11",
        *m255.EXEC_MARKERS,
        *([b"[DYN] real ELF interpreter loaded, entry=0x1234"] * 5),
        uart.RUN_MARKERS[2],
        uart.SIGNED_EXEC,
        *uart.RUN_MARKERS[3:],
    )) + b"\n"


class M256SignedTelinit(unittest.TestCase):
    def test_exact_signed_original_sysvinit_package_only(self):
        self.assertEqual(prov.m255.PACKAGE, "sysvinit-core")
        self.assertEqual(prov.SOURCE, "usr/sbin/telinit")
        self.assertEqual(prov.TARGET, "usr/sbin/telinit")
        source = (ROOT / "scripts/m256-debian-provenance.py").read_text()
        for value in ("verified_original", "m245.sha256_file", "dpkg-deb",
                      "m248.original_regular_file", "m255.init_elf",
                      "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS"):
            self.assertIn(value, source)
        with self.assertRaises(ValueError):
            prov.require(False, "unverified original")


class M256GuestInitctl(unittest.TestCase):
    def test_tmpfs_provider_accepts_fifo_as_well_as_unix_socket(self):
        provider = (ROOT / "kernel/fs/vfs_providers.c").read_text()
        start = provider.index("static int tmp_mknod_node(")
        end = provider.index("static int64_t tmp_write_node(", start)
        segment = provider[start:end]
        self.assertIn("type!=0140000ULL && type!=0010000ULL", segment)
        self.assertIn("tmp_mknod_at(parent->inode,name", segment)

    def test_mount_helper_is_actual_mount_then_sigusr1(self):
        src = ROOT / "tests/m256-mount-run.c"
        result = subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                 "-fsyntax-only", str(src)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        code = src.read_text()
        self.assertLess(code.index("mount("), code.index("kill(1, SIGUSR1)"))
        self.assertIn('"/run"', code)
        self.assertNotIn('mkfifo("/run/initctl"', code)

    def test_reviewed_inittab_real_control_and_runlevel(self):
        self.assertIn(b"si::sysinit:/etc/m256-guest-sysinit.sh", disk.INITTAB)
        self.assertIn(b"ct:2:once:/etc/m256-guest-telinit.sh", disk.INITTAB)
        self.assertIn(b"l3:3:once:/etc/m256-guest-runlevel3.sh", disk.INITTAB)
        self.assertIn(b"sv:2:respawn:/etc/m255-guest-service.sh", disk.INITTAB)
        for path in disk.SCRIPTS.values():
            self.assertEqual(subprocess.run(["dash", "-n", str(path)],
                                            capture_output=True).returncode, 0)
        ctl = (ROOT / "scripts/m256-guest-telinit.sh").read_text()
        self.assertIn("[ ! -p /run/initctl ]", ctl)
        self.assertIn("/usr/sbin/telinit 3", ctl)
        self.assertIn("[M256 INITCTL FAIL]", ctl)

    def test_stage_never_preseeds_fifo_or_guest_result(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "etc").mkdir()
            result = subprocess.run(["python3", str(ROOT / "scripts/m256-debian-ext4-disk.py"),
                                     "stage", str(root)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root / "etc/inittab").read_bytes(), disk.INITTAB)
            self.assertTrue((root / "run").is_dir())
            self.assertFalse((root / "run/initctl").exists())
            self.assertFalse((root / "var/tmp/m255-sysvinit").exists())


class M256RealUART(unittest.TestCase):
    def test_original_fifo_telinit_and_runlevel_evidence(self):
        self.assertEqual(uart.inspect(fake()), {"first_pid": 8, "respawn_pid": 11})
        for corrupted in (
            fake().replace(uart.SIGNED_EXEC, b"FAKE signed telinit"),
            fake().replace(uart.RUN_MARKERS[0], b"not mounted"),
            fake().replace(uart.RUN_MARKERS[-1], b"echo instead of runlevel"),
            fake().replace(uart.RUN_MARKERS[3], b"no actual PID1 runlevel switch"),
            fake().replace(b"M255-SERVICE-RESPAWN-PID=11", b"M255-SERVICE-RESPAWN-PID=8"),
            fake().replace(uart.RUN_MARKERS[2], b"[M256 INITCTL FAIL] cannot open"),
        ):
            with self.subTest(corrupted=corrupted[:80]):
                with self.assertRaises(ValueError):
                    uart.inspect(corrupted)

    def test_no_synthetic_shutdown_pass(self):
        script = (ROOT / "scripts/m256-debian-uart.py").read_text()
        self.assertIn("DIAGNOSTIC VERIFIED", script)
        self.assertIn("poweroff/durable ext4 NOT YET PASS", script)
        self.assertNotIn("[PASS] cmdline userspace exit 0", script)
        self.assertIn('b"[M256 INITCTL FAIL]" in seen', script)


class M256NativeGate(unittest.TestCase):
    def test_one_guest_boot_and_explicit_profile_exclusion(self):
        script = (ROOT / "scripts/m252-probe-debian-text-tools.sh").read_text()
        for text in ("NAS_ENABLE_M256_DEBIAN_INITCTL", "m256-initctl",
                     "m256-debian-provenance.py prepare",
                     "m256-debian-ext4-disk.py before",
                     "m256-debian-uart.py"):
            self.assertIn(text, script)
        self.assertNotIn("NAS_M256_PLATFORM_POWERDOWN=1", script)
        for path in ("probe-x86-busybox-parity.sh",
                     "probe-riscv64-busybox-matrix.sh",
                     "probe-aarch64-busybox-matrix.sh"):
            text = (ROOT / "scripts" / path).read_text()
            self.assertIn("m256-initctl)", text)
            self.assertIn("NAS_M256_QEMU_TIMEOUT:-480", text)


if __name__ == "__main__":
    unittest.main()
