#!/usr/bin/env python3
"""Host-only negative/positive tests; no QEMU success is inferred here."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location(
    "native_perf_report", HERE / "native-perf-report.py")
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class ProfileTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.work = Path(tmp.name)

    def write(self, name, body):
        path = self.work / name
        path.write_text(body, encoding="utf-8")
        return path

    @staticmethod
    def metadata():
        return dict(workload="m56", port="riscv64", qemu_version="9.2",
                    qemu_machine="virt", qemu_args="-smp 1 -m 512M",
                    rootfs_sha256="a" * 64, source_sha256="b" * 64,
                    toolchain="Alpine gcc 15", build_flags="-O2",
                    cache_state="cold", guest_ram_mib=512, vcpus=1,
                    instrumentation="none")

    def test_manifest_requires_identical_runtime_and_source(self):
        a = self.write("a.json", json.dumps(self.metadata()))
        b = self.write("b.json", json.dumps(self.metadata()))
        report.check_manifests([a], [b], "m56", "riscv64")
        altered = self.metadata()
        altered["source_sha256"] = "c" * 64
        self.write("b.json", json.dumps(altered))
        with self.assertRaisesRegex(ValueError, "incomparable inputs"):
            report.check_manifests([a], [b], "m56", "riscv64")
        del altered["toolchain"]
        self.write("b.json", json.dumps(altered))
        with self.assertRaisesRegex(ValueError, "incomplete manifest"):
            report.load_manifest(b)

    def test_counter_presence_and_integrity(self):
        log = self.write("counter.log", "\n".join((
            "NAS_PERF_COUNTER workload=m56 scope=cpu metric=kbuild.busy value=121 unit=cs",
            "NAS_PERF_COUNTER workload=m56 scope=cpu metric=kbuild.switches value=8 unit=count",
        )))
        counters = report.parse_counters(log, "m56")
        self.assertEqual(counters["cpu.kbuild.busy.cs"], 121)
        rendered = report.counter_table([counters, counters], [counters, counters])
        self.assertIn("  syscall: not-measured", rendered)
        self.assertIn("  vfs: not-measured", rendered)
        with self.assertRaisesRegex(ValueError, "availability differs"):
            report.counter_table([counters], [{}])
        self.write("counter.log", log.read_text() + "\n" + log.read_text().splitlines()[0])
        with self.assertRaisesRegex(ValueError, "duplicate counter"):
            report.parse_counters(log, "m56")

    def test_real_guest_stat_wrapper_protocol_without_guest_claim(self):
        stat = self.write("stat", "cpu  0 0 100 20 0 0 0 0 0 0\nctxt 7\n")
        update = "from pathlib import Path; Path(sys.argv[1]).write_text(" \
                 "'cpu  0 0 140 25 0 0 0 0 0 0\\nctxt 13\\n')"
        cmd = ["sh", str(HERE / "native-profile-guest.sh"),
               "m56", "kbuild", "--", sys.executable, "-c",
               "import sys; " + update, str(stat)]
        env = dict(os.environ, NAS_PROFILE_STAT_FILE=str(stat))
        output = subprocess.run(cmd, capture_output=True, text=True, env=env)
        self.assertEqual(output.returncode, 0, output.stderr)
        self.assertIn("metric=kbuild.busy value=40 unit=cs", output.stdout)
        self.assertIn("metric=kbuild.idle value=5 unit=cs", output.stdout)
        self.assertIn("metric=kbuild.switches value=6 unit=count", output.stdout)
        failure = subprocess.run(["sh", str(HERE / "native-profile-guest.sh"),
                                  "m56", "kbuild", "--", "sh", "-c", "exit 17"],
                                 capture_output=True, text=True, env=env)
        self.assertEqual(failure.returncode, 17)
        self.assertNotIn("NAS_PERF_COUNTER", failure.stdout)

    def test_original_source_build_phase_emits_cpu_sample(self):
        """A host procfs fixture exercises the SAME sourced guest phase hook."""
        stat = self.write("phase-stat", "cpu  0 0 110 23 0 0 0 0 0 0\nctxt 7\n")
        code = ('set -eu; . "$1"; nas_perf_begin m56; '
                'printf "cpu  0 0 158 29 0 0 0 0 0 0\\nctxt 14\\n" > "$2"; '
                'nas_perf_phase kbuild')
        env = dict(os.environ, NAS_PROFILE_STAT_FILE=str(stat),
                   NAS_PROFILE_HELPER_FILE=str(HERE / "native-profile-guest.sh"))
        sampled = subprocess.run(["sh", "-c", code, "guest-fixture",
                                  str(HERE.parent / "tests/native-perf-clock.sh"),
                                  str(stat)], capture_output=True, text=True, env=env)
        self.assertEqual(sampled.returncode, 0, sampled.stderr)
        self.assertIn("NAS_PERF_PHASE workload=m56 phase=kbuild elapsed_cs=",
                      sampled.stdout)
        self.assertIn("NAS_PERF_COUNTER workload=m56 scope=cpu "
                      "metric=kbuild.busy value=48 unit=cs", sampled.stdout)
        self.assertIn("metric=kbuild.idle value=6 unit=cs", sampled.stdout)
        self.assertIn("metric=kbuild.switches value=7 unit=count", sampled.stdout)

        # Feed actual hook output to the unchanged report parser, beside
        # other fixture phase/acceptance lines. No guest PASS is claimed.
        log = self.write("phase-fixture.log", "\n".join((
            "NAS_PERF_PHASE workload=m56 phase=source_copy elapsed_cs=5",
            "NAS_PERF_PHASE workload=m56 phase=kconfig elapsed_cs=10",
            sampled.stdout.strip(),
            "M56_REAL_GUEST_BUSYBOX_FULL_PASS", "[PASS] cmdline userspace exit 0",
        )))
        self.assertEqual(report.parse_log(log, "m56")["kbuild"],
                         int(next(line.rsplit("=", 1)[1]
                                  for line in sampled.stdout.splitlines()
                                  if line.startswith("NAS_PERF_PHASE"))))
        counters = report.parse_counters(log, "m56")
        self.assertEqual(counters["cpu.kbuild.busy.cs"], 48)
        self.assertEqual(counters["cpu.kbuild.switches.count"], 7)

    def test_bad_proc_stat_does_not_fake_counter_or_break_phase(self):
        stat = self.write("bad-stat", "cpu  123 4 30 10 0 0 0 0 0 0\nctxt 6\n")
        code = ('set -eu; . "$1"; nas_perf_begin m38; '
                'printf "cpu  123 4 40 11 0 0 0 0 0 0\\nctxt 9\\n" > "$2"; '
                'nas_perf_phase gcc_hello')
        env = dict(os.environ, NAS_PROFILE_STAT_FILE=str(stat),
                   NAS_PROFILE_HELPER_FILE=str(HERE / "native-profile-guest.sh"))
        result = subprocess.run(["sh", "-c", code, "guest-fixture",
                                 str(HERE.parent / "tests/native-perf-clock.sh"),
                                 str(stat)], capture_output=True, text=True, env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("NAS_PERF_PHASE workload=m38 phase=gcc_hello", result.stdout)
        self.assertNotIn("NAS_PERF_COUNTER", result.stdout)

    def test_missing_helper_and_backwards_stat_are_not_measured(self):
        stat = self.write("backward-stat", "cpu  0 0 200 60 0 0 0 0 0 0\nctxt 30\n")
        code = ('set -eu; . "$1"; nas_perf_begin m55; '
                'printf "cpu  0 0 199 61 0 0 0 0 0 0\\nctxt 29\\n" > "$2"; '
                'nas_perf_phase ninja_build')
        clock = str(HERE.parent / "tests/native-perf-clock.sh")
        env = dict(os.environ, NAS_PROFILE_STAT_FILE=str(stat),
                   NAS_PROFILE_HELPER_FILE=str(HERE / "native-profile-guest.sh"))
        result = subprocess.run(["sh", "-c", code, "guest-fixture", clock,
                                 str(stat)], capture_output=True, text=True, env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("NAS_PERF_PHASE workload=m55 phase=ninja_build", result.stdout)
        self.assertNotIn("NAS_PERF_COUNTER", result.stdout)
        env["NAS_PROFILE_HELPER_FILE"] = str(self.work / "not-staged.sh")
        result = subprocess.run(["sh", "-c", code, "guest-fixture", clock,
                                 str(stat)], capture_output=True, text=True, env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("NAS_PERF_PHASE workload=m55 phase=ninja_build", result.stdout)
        self.assertNotIn("NAS_PERF_COUNTER", result.stdout)

    def test_original_guest_scripts_and_rootfs_stage_same_profile_hook(self):
        root = HERE.parent
        for name in ("m38-gcc-build.sh", "m55-cjson-run.sh", "m56-busybox-run.sh"):
            self.assertIn(". /tests/native-perf-clock.sh",
                          (root / "tests" / name).read_text(encoding="utf-8"))
        self.assertIn('cp scripts/native-profile-guest.sh '
                      '"$root/tests/native-profile-guest.sh"',
                      (root / "rootfs/alpine/build.sh").read_text(encoding="utf-8"))

    def test_strace_profile_requires_actual_summary(self):
        original = self.write("strace.log", """% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 70.00    0.000700          70        10           read
 30.00    0.000300         100         3         1 openat
------ ----------- ----------- --------- --------- ----------------
100.00    0.001000          77        13         1 total
""")
        args = [sys.executable, str(HERE / "native-profile-strace.py"),
                "--workload", "m56", "--phase", "kbuild", "--input", str(original),
                "--syscalls", "read,openat"]
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("metric=kbuild.total.time value=1000000 unit=ns", result.stdout)
        self.assertIn("metric=kbuild.read.calls value=10 unit=count", result.stdout)
        bad = self.write("bad.log", "not a strace summary\n")
        args[args.index(str(original))] = str(bad)
        result = subprocess.run(args, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
