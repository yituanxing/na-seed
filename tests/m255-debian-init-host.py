#!/usr/bin/env python3
"""Offline M255 contracts. No test here may claim native-QEMU success."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def load(file: str, module: str):
    spec = importlib.util.spec_from_file_location(module, ROOT / "scripts" / file)
    assert spec and spec.loader
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


prov = load("m255-debian-provenance.py", "m255_provenance_contract_host")
disk = load("m255-debian-ext4-disk.py", "m255_disk_contract_host")
uart = load("m255-debian-uart.py", "m255_uart_contract_host")


def fake_uart(first: bytes = b"31", second: bytes = b"34") -> bytes:
    return b"\n".join((
        *uart.REQUIRED_ORDER[:4],
        b"M255-SERVICE-FIRST-PID=" + first,
        b"M255-SERVICE-RESPAWN-PID=" + second,
        *uart.EXEC_MARKERS,
        *([b"[DYN] real ELF interpreter loaded, entry=0x1234"] * 5),
    )) + b"\n"


class DebianSysvinitProvenanceHost(unittest.TestCase):
    def test_signed_original_init_not_build_artifact(self):
        self.assertEqual(prov.PACKAGE, "sysvinit-core")
        self.assertEqual(prov.PROGRAM, "sbin/init")
        # Debian 13's merged-usr package installs the source ELF here on all
        # three ISAs, even though Limine requests /sbin/init as guest PID1.
        self.assertEqual(prov.SOURCE_PROGRAM, "usr/sbin/init")
        self.assertEqual(prov.SCHEMA, "m255-signed-original-debian-sysvinit-pid1-v1")
        self.assertIn("libselinux.so.1", prov.LIBRARY_PACKAGES)
        source = (ROOT / "scripts/m255-debian-provenance.py").read_text()
        for required in ("signed_dependency_record", "fetch_and_extract",
                         "m245.sha256_file", "original_regular_file", "elf_interpreter",
                         "source_package", "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS"):
            # Either source-code spelling from the reused M252 helper or
            # reviewed direct use must remain visible in the M255 source.
            if required == "signed_dependency_record":
                self.assertIn("m252.package_record", source)
            elif required == "elf_interpreter":
                self.assertIn("m245.elf_interpreter", source)
            else:
                self.assertIn(required, source)

    def test_unreviewed_dependency_rejected(self):
        with self.assertRaisesRegex(ValueError, "unknown signed SONAME"):
            prov.require(False, "unknown signed SONAME")
        self.assertNotIn("libinvented.so.1", prov.LIBRARY_PACKAGES)


class DebianSysvinitDiskHost(unittest.TestCase):
    def test_inittab_real_pid1_owns_sysinit_and_respawn(self):
        self.assertEqual(disk.INITTAB, (
            b"id:2:initdefault:\n"
            b"si::sysinit:/etc/m255-guest-sysinit.sh\n"
            b"sv:2:respawn:/etc/m255-guest-service.sh\n"))
        self.assertNotIn(b"m250-tty-launch", disk.INITTAB)

    def test_guest_scripts_compile_with_real_dash_and_have_distinct_pids(self):
        for relative, source in disk.SCRIPTS.items():
            with self.subTest(script=relative):
                self.assertEqual(subprocess.run(["dash", "-n", str(source)],
                                                capture_output=True).returncode, 0)
                content = source.read_text()
                self.assertTrue(content.startswith("#!/usr/bin/dash\n"))
                self.assertIn("/dev/console", content)
        service = (ROOT / "scripts/m255-guest-service.sh").read_text()
        self.assertIn('FIRST-PID=$$', service)
        self.assertIn('RESPAWN-PID=$$', service)
        self.assertLess(service.index("exit 0"), service.index("RESPAWN-PID=$$"))
        self.assertIn("exec /usr/bin/sleep 3600", service)

    def test_stage_command_is_real_and_never_preseeds_result(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "etc").mkdir()
            result = subprocess.run(
                ["python3", str(ROOT / "scripts/m255-debian-ext4-disk.py"),
                 "stage", str(root)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((root / "etc/inittab").read_bytes(), disk.INITTAB)
            for relative, source in disk.SCRIPTS.items():
                self.assertEqual((root / relative).read_bytes(), source.read_bytes())
            self.assertFalse((root / "var/tmp/m255-sysvinit").exists())


class DebianSysvinitUartHost(unittest.TestCase):
    def test_real_ordered_pid1_and_distinct_respawn_pids(self):
        self.assertEqual(uart.inspect(fake_uart()),
                         {"first_pid": 31, "respawn_pid": 34})
        for broken in (fake_uart(second=b"31"),
                       fake_uart().replace(b"[M255 ROOT]", b"[M191 ROOT]"),
                       fake_uart().replace(b"[EXEC] replaced", b"[FAKE] replaced"),
                       fake_uart().replace(b"M255-SERVICE-RESPAWN-PID=", b"noise-M255-SERVICE-RESPAWN-PID="),
                       fake_uart().replace(b"M255-SERVICE-FIRST-PID=", b"M255-SERVICE-FIRST-PID=oops"),
                       fake_uart().replace(b"M255-SERVICE-FIRST-PID=31", b"M255-SERVICE-FIRST-PID=31\n[FAIL] guest")):
            with self.subTest(broken=broken[:100]):
                with self.assertRaises(ValueError):
                    uart.inspect(broken)

    def test_runner_never_calls_poweroff_or_claims_ext4_pass(self):
        runner = (ROOT / "scripts/m255-debian-uart.py").read_text()
        self.assertIn("DIAGNOSTIC VERIFIED", runner)
        self.assertIn("poweroff/durable ext4 NOT YET PASS", runner)
        self.assertIn("os.killpg", runner)
        self.assertNotIn("[PASS] cmdline userspace exit 0", runner)


class DebianSysvinitIntegrationHost(unittest.TestCase):
    def test_single_native_qemu_uses_original_debian_init_pid1(self):
        launcher = (ROOT / "scripts/m252-probe-debian-text-tools.sh").read_text()
        for required in ("NAS_ENABLE_M255_DEBIAN_INIT", "NAS_ENABLE_M254_DEBIAN_SHELL",
                         "m255-init", "m255-debian-provenance.py prepare",
                         "m255-debian-ext4-disk.py stage", "m255-debian-uart.py",
                         "' ' - - /sbin/init", "clean poweroff and durable ext4 NOT YET PASS"):
            self.assertIn(required, launcher)
        self.assertNotIn("m255-init\t", (ROOT / "scripts/m252-debian-uart.py").read_text())

    def test_reusable_workflow_and_three_isa_long_run(self):
        wrapper = (ROOT / ".github/workflows/m255-debian-init-service.yml").read_text()
        reused = (ROOT / ".github/workflows/m252-debian-text-tools-ext4.yml").read_text()
        self.assertIn("feat/m255-original-debian-init-20261110", wrapper)
        self.assertIn("m255_init: true", wrapper)
        self.assertIn("m255_init:", reused)
        self.assertIn("NAS_ENABLE_M255_DEBIAN_INIT", reused)
        self.assertIn("m255-extra.json", reused)
        self.assertIn("port: [amd64, riscv64, arm64]", reused)
        self.assertIn("-m252-native-worker", reused)
        for wrapper_name in ("probe-x86-busybox-parity.sh",
                             "probe-riscv64-busybox-matrix.sh",
                             "probe-aarch64-busybox-matrix.sh"):
            with self.subTest(isa=wrapper_name):
                script = (ROOT / "scripts" / wrapper_name).read_text()
                self.assertIn("m255-init)", script)
                self.assertIn("NAS_M255_QEMU_TIMEOUT:-420", script)


if __name__ == "__main__":
    unittest.main()
