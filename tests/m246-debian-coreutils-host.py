#!/usr/bin/env python3
"""Offline fail-closed contracts for the original Debian glibc M246 batch.

These are host-side checks, NOT proof of a successful guest execution.
Only the QEMU serial oracles in m246-probe-debian-coreutils.sh can PASS.
"""

from pathlib import Path
import subprocess
import unittest


PROBE = Path("scripts/m246-probe-debian-coreutils.sh")
FETCH = Path("scripts/m245-fetch-debian-dynamic.py")


class DebianGlibcBatchContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.probe = PROBE.read_text(encoding="utf-8")
        cls.fetch = FETCH.read_text(encoding="utf-8")

    def test_wrong_arch_refuses_without_network(self):
        for argv in ([], ["arm64"], ["amd64", "extra"]):
            with self.subTest(argv=argv):
                result = subprocess.run(["sh", str(PROBE), *argv],
                                        stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, timeout=5)
                self.assertEqual(result.returncode, 2)

    def test_reuses_signed_exact_debian_packages(self):
        self.assertIn("scripts/m245-fetch-debian-dynamic.py --arch amd64", self.probe)
        self.assertIn("test -s \"$work/source/manifest.json\"", self.probe)
        self.assertIn('run("apt-get", *options, "update")', self.fetch)
        self.assertIn("fetch_verified_deb(record, target)", self.fetch)
        self.assertIn("validate_sysroot(root)", self.fetch)

    def test_untouched_elf_and_bounded_dependencies(self):
        self.assertIn("programs='true cat wc printf'", self.probe)
        self.assertIn('readelf -l "$binary"', self.probe)
        self.assertIn('if [ "$needed" != libc.so.6 ]', self.probe)
        self.assertIn('cmp "$source/usr/bin/$program" "$verify/usr/bin/$program"', self.probe)
        self.assertIn('cmp "$source/usr/lib/x86_64-linux-gnu/$lib"', self.probe)
        self.assertIn('cpio -id --quiet -D "$verify" < "$archive"', self.probe)

    def test_single_real_qemu_with_four_absolute_execs(self):
        self.assertEqual(self.probe.count("sh scripts/probe-x86-busybox-parity.sh"), 1)
        for program in ("true", "cat", "wc", "printf"):
            self.assertIn(f"/usr/bin/{program}", self.probe)
        self.assertIn("NAS_ACCEPTANCE_CPIO=\"$archive\"", self.probe)
        self.assertIn("NAS_SYSCALL_TRACE=1", self.probe)
        self.assertIn("exec /usr/bin/printf", self.probe)
        # The Limine argv parser consumes one slash; the guest must receive
        # the backslash of \n, not emit a literal trailing "n".
        self.assertIn(r"M246-GLIBC-BATCH-OK\\n", self.probe)

    def test_guest_only_serial_oracles_and_no_fake_pass(self):
        self.assertIn('grep -Fq "[EXEC] replaced process with CPIO ELF: /usr/bin/$program" "$log"',
                      self.probe)
        self.assertIn("dyn_count=$(grep -Fc '[DYN] real ELF interpreter loaded, entry=' \"$log\")",
                      self.probe)
        self.assertIn('grep -Fxq M246-CAT-OK "$log"', self.probe)
        self.assertIn('grep -Fxq M246-GLIBC-BATCH-OK "$log"', self.probe)
        self.assertIn('grep -Eq ', self.probe)
        self.assertIn("[PASS] cmdline userspace exit 0", self.probe)
        self.assertLess(self.probe.index("sh scripts/probe-x86-busybox-parity.sh"),
                        self.probe.index("[M246 ORIGINAL DEBIAN GLIBC COREUTILS REAL X86 QEMU PASS]"))


if __name__ == "__main__":
    unittest.main()
