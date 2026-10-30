#!/usr/bin/env python3
"""Host-only validation of the genuine guest performance log contract."""
import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("native-perf-report.py")
spec = importlib.util.spec_from_file_location("native_perf_report", SCRIPT)
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class NativePerfReportTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)

    def log(self, name, phases, *, complete=True, failure=False):
        lines = [
            f"NAS_PERF_PHASE workload=m56 phase={phase} elapsed_cs={centis}"
            for phase, centis in phases
        ]
        if complete:
            lines.extend(("M56_REAL_GUEST_BUSYBOX_FULL_PASS",
                          "[PASS] cmdline userspace exit 0"))
        if failure:
            lines.append("[FAIL] original busybox Kbuild failed")
        path = Path(self.scratch.name) / name
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        return path

    def test_verified_original_application_with_measured_phases(self):
        before = self.log("before", (("source_copy", 125),
                                    ("kconfig", 320), ("kbuild", 4000)))
        after = self.log("after", (("source_copy", 125),
                                  ("kconfig", 320), ("kbuild", 2000)))
        a = report.parse_log(before, "m56")
        b = report.parse_log(after, "m56")
        rendered = report.render("m56", "riscv64", [a, a], [b, b],
                                 "base-sha", "candidate-sha")
        self.assertIn("kbuild", rendered)
        self.assertIn("2.00x", rendered)
        self.assertNotIn("EXPLORATORY", rendered)

    def test_incomplete_or_failed_guest_cannot_produce_a_speedup(self):
        phases = (("source_copy", 1), ("kconfig", 1), ("kbuild", 1))
        for name, complete, failure in (("partial", False, False),
                                        ("failed", True, True)):
            with self.subTest(name=name), self.assertRaises(ValueError):
                report.parse_log(self.log(name, phases, complete=complete,
                                          failure=failure), "m56")

    def test_missing_or_duplicate_measurements_are_rejected(self):
        phases = (("source_copy", 1), ("kconfig", 1), ("kbuild", 1))
        for name, records in (("missing", phases[:2]),
                              ("duplicate", phases + (phases[2],)),
                              ("reordered", tuple(reversed(phases)))):
            with self.subTest(name=name), self.assertRaises(ValueError):
                report.parse_log(self.log(name, records), "m56")

    def test_malformed_measurement_is_rejected(self):
        with self.assertRaises(ValueError):
            report.parse_log(self.log("broken", (("source_copy", "-1"),)),
                             "m56")


if __name__ == "__main__":
    unittest.main()
