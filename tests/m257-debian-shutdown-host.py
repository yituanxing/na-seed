#!/usr/bin/env python3
"""M257 offline policy tests; fake disk dictionaries are NOT QEMU evidence."""

import importlib.util
import contextlib
import io
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]


def load(name: str, module: str):
    spec = importlib.util.spec_from_file_location(module, ROOT / "scripts" / name)
    assert spec and spec.loader
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


prov = load("m257-debian-provenance.py", "m257_provenance_unit")
disk = load("m257-debian-ext4-disk.py", "m257_ext4_unit")
uart = load("m257-debian-uart.py", "m257_uart_unit")


class M257SignedOriginals(unittest.TestCase):
    def test_only_reviewed_signed_original_halt_and_sync(self):
        self.assertEqual(prov.PROGRAMS,
                         {"usr/sbin/halt": "sysvinit-core", "usr/bin/sync": "coreutils"})
        source = (ROOT / "scripts/m257-debian-provenance.py").read_text()
        for expected in ("verified_original", "verified_coreutils_extract",
                         "original_regular_file", "copy_signed_bytes",
                         "m255.init_elf", "m256_manifest_sha256",
                         "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS"):
            self.assertIn(expected, source)
        with self.assertRaises(ValueError):
            prov.require(False, "unsigned")

    def test_fake_halt_elf_and_mutated_signed_hash_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            area = Path(temp)
            root = area / "root"
            halt = area / "signed-halt"
            sync = area / "signed-sync"
            halt.write_bytes(b"fake non-ELF halt")
            sync.write_bytes(b"fake non-ELF sync")
            original = {"usr/sbin/halt": halt, "usr/bin/sync": sync}
            m255_path = area / "m255.json"
            m256_path = area / "m256.json"
            m256_path.write_text("signed-ancestor-for-unit-test")
            m257_path = area / "m257.json"
            paths = (root, area / "base", area / "m251", area / "m252",
                     m255_path, m256_path, m257_path)
            with patch.object(prov, "original_sources", return_value=original):
                # The real readelf/native ABI guard must reject a fake input.
                with self.assertRaises(Exception):
                    prov.records("amd64", root, area / "base", m255_path,
                                 stage=True)
                with (patch.object(prov.m255, "init_elf",
                                   return_value={"dt_needed": ["libc.so.6"],
                                                 "interpreter": "test-only"}),
                      patch.object(prov.m256, "verify", return_value={})):
                    programs = prov.records("amd64", root, area / "base",
                                            m255_path, stage=True)
                    fixture = {"schema": prov.SCHEMA,
                               "status": "HOST_PREPARED_ONLY_NOT_NATIVE_QEMU_PASS",
                               "architecture": "amd64",
                               "m256_manifest_sha256": prov.m245.sha256_file(m256_path),
                               "programs": programs}
                    m257_path.write_text(json.dumps(fixture))
                    prov.verify("amd64", *paths)
                    (root / "usr/sbin/halt").write_bytes(b"FAKE REPLACEMENT ELF")
                    with self.assertRaisesRegex(ValueError, "changed on root"):
                        prov.verify("amd64", *paths)
                    (root / "usr/sbin/halt").write_bytes(halt.read_bytes())
                    halt.write_bytes(b"SOURCE SHA DRIFT")
                    with self.assertRaisesRegex(ValueError, "changed on root"):
                        prov.verify("amd64", *paths)


class M257ReviewedGuest(unittest.TestCase):
    def test_host_stage_mints_random_input_but_never_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "etc").mkdir()
            disk.stage(root)
            nonce = (root / disk.NONCE_INPUT).read_bytes()
            self.assertRegex(nonce, re.compile(disk.NONCE_REGEX))
            self.assertEqual((root / "etc/inittab").read_bytes(), disk.INITTAB)
            self.assertFalse((root / disk.NONCE_OUTPUT).exists())
            disk.stage(root)
            self.assertEqual((root / disk.NONCE_INPUT).read_bytes(), nonce)
            for path in disk.SCRIPTS.values():
                self.assertEqual(subprocess.run(["dash", "-n", str(path)],
                                                capture_output=True).returncode, 0)
            (root / disk.NONCE_OUTPUT).parent.mkdir(parents=True)
            (root / disk.NONCE_OUTPUT).write_bytes(nonce)
            with self.assertRaisesRegex(ValueError, "preseed"):
                disk.stage(root)

    def test_reviewed_init_uses_real_telinit_and_halt(self):
        self.assertIn(b"ct:2:once:/etc/m256-guest-telinit.sh", disk.INITTAB)
        self.assertIn(b"l3:3:once:/etc/m256-guest-runlevel3.sh", disk.INITTAB)
        self.assertIn(b"zs:3:once:/etc/m257-guest-telinit.sh", disk.INITTAB)
        self.assertIn(b"l0:0:wait:/etc/m257-guest-halt.sh", disk.INITTAB)
        ctl = (ROOT / "scripts/m257-guest-telinit.sh").read_text()
        halt = (ROOT / "scripts/m257-guest-halt.sh").read_text()
        self.assertIn("[ ! -p /run/initctl ]", ctl)
        self.assertIn("/usr/sbin/telinit 0", ctl)
        self.assertIn("/usr/bin/sleep 1", ctl)
        self.assertIn("/usr/bin/cat /etc/m257-nonce-input >", halt)
        self.assertIn("/usr/bin/sync", halt)
        self.assertIn("exec /usr/sbin/halt -p -f", halt)
        self.assertNotIn("kill -9", halt)


class M257PhysicalOracle(unittest.TestCase):
    def test_pre_anti_preseed_and_post_clean_nonce_gate(self):
        with tempfile.TemporaryDirectory() as temp:
            area = Path(temp)
            root = area / "root"
            (root / "etc").mkdir(parents=True)
            disk.stage(root)
            for relative in disk.READBACK:
                path = root / relative
                if path.exists():
                    continue
                path.parent.mkdir(parents=True, exist_ok=True)
                source = (disk.m256disk.SCRIPTS | disk.m255disk.SCRIPTS).get(relative)
                path.write_bytes(source.read_bytes() if source else b"signed-original-test-elf")
                if relative.startswith("etc/"):
                    path.chmod(0o755)
            physical = {relative: (root / relative).read_bytes()
                        for relative in disk.READBACK}
            fake_disk = area / "m257.img"
            fake_disk.write_bytes(b"fake physical image ONLY for host unit test")
            paths = [root, fake_disk, *(area / label for label in
                       ("base", "m251", "m252", "m255", "m256", "m257"))]

            def fake_extract(_disk: Path, destination: Path) -> None:
                destination.write_bytes(b"fake-unit-ext4")

            with (patch.object(disk.prov, "verify", return_value={}),
                  patch.object(disk.m191, "check_mbr", return_value=None),
                  patch.object(disk.m191, "extract_partition", side_effect=fake_extract),
                  patch.object(disk.m191, "check_fsck", return_value=None),
                  patch.object(disk.m191, "debugfs_cat",
                               side_effect=lambda _partition, path:
                               physical.get(path.lstrip("/"), b""))):
                # A synthetic host fixture must never emit a real QEMU PASS
                # marker into CI logs; suppress the oracle's status text.
                with contextlib.redirect_stdout(io.StringIO()):
                    disk.verify("amd64", *paths, after=False)
                    physical[disk.NONCE_OUTPUT] = (root / disk.NONCE_INPUT).read_bytes()
                    with self.assertRaisesRegex(ValueError, "preseeded"):
                        disk.verify("amd64", *paths, after=False)
                    del physical[disk.NONCE_OUTPUT]
                    with self.assertRaisesRegex(ValueError, "not persisted"):
                        disk.verify("amd64", *paths, after=True)
                    physical.update(disk.GUEST_STATE)
                    physical[disk.NONCE_OUTPUT] = (root / disk.NONCE_INPUT).read_bytes()
                    disk.verify("amd64", *paths, after=True)
                    physical[disk.NONCE_OUTPUT] = b"FORGED-NONCE\n"
                    with self.assertRaisesRegex(ValueError, "mismatched"):
                        disk.verify("amd64", *paths, after=True)


class M257TrueNativePoweroffGate(unittest.TestCase):
    def test_requires_ordered_original_telinit_halt_kernel_unmount_and_platform(self):
        m255 = uart.m255
        fake = b"\n".join((
            *m255.REQUIRED_ORDER[:3],
            *uart.REQUIRED_IN_ORDER,
            *m255.EXEC_MARKERS,
            b"[EXEC] replaced process with lwext4 ELF: /usr/bin/sync",
            *([b"[DYN] real ELF interpreter loaded, entry=0x1234"] * 5),
            b"[ARM64] userspace debug-exit status=0",
        )).replace(b"M255-SERVICE-FIRST-PID=", b"M255-SERVICE-FIRST-PID=8").replace(
            b"M255-SERVICE-RESPAWN-PID=", b"M255-SERVICE-RESPAWN-PID=11") + b"\n"
        self.assertEqual(uart.inspect(fake, "arm64"),
                         {"first_pid": 8, "respawn_pid": 11})
        for victim in (uart.REQUIRED_IN_ORDER[-1],
                       uart.REQUIRED_IN_ORDER[-2],
                       uart.REQUIRED_IN_ORDER[-3],
                       uart.REQUIRED_IN_ORDER[-4],
                       b"[ARM64] userspace debug-exit status=0"):
            with self.subTest(victim=victim):
                with self.assertRaises(ValueError):
                    uart.inspect(fake.replace(victim, b"M257-MISSING"), "arm64")
        with self.assertRaises(ValueError):
            uart.inspect(fake + b"[M257 SHUTDOWN FAIL] synthetic marker\n", "arm64")

    def test_no_host_kill_as_pass_or_unsigned_generic_reboot(self):
        policy = (ROOT / "kernel/init/main.c").read_text()
        dispatch = (ROOT / "kernel/syscall/dispatch.c").read_text()
        exe = (ROOT / "kernel/task/exec.c").read_text()
        runner = (ROOT / "scripts/m257-debian-uart.py").read_text()
        self.assertIn('strcmp(actual,"/usr/sbin/halt")==0', exe)
        self.assertIn('caller->m257_original_debian_halt', policy)
        self.assertIn('caller->ppid!=1', policy)
        self.assertIn('command!=NAS_LINUX_REBOOT_POWER_OFF', policy)
        self.assertIn('nas_init_debian_power_request(&TASK,command)', dispatch)
        self.assertIn('if code:', runner)
        self.assertIn('if proc is not None and proc.poll() is None:', runner)
        self.assertNotIn('os.killpg(proc.pid, signal.SIGTERM)\n                    return', runner)


if __name__ == "__main__":
    unittest.main()
