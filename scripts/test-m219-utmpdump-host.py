#!/usr/bin/env python3
"""M219 host negative probes; NEVER count as original-ELF guest evidence."""

from __future__ import annotations

from pathlib import Path
import json
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "tests/m219-utmpdump-fixture.c"
WORKLOAD = ROOT / "tests/m219-utmpdump-workload.sh"
OWNER = ROOT / "tests/m205-software-workload.sh"
RUNNER = ROOT / "scripts/probe-m197-alpine-software-cohort.sh"
MANIFEST = ROOT / "scripts/m219-candidate-elfs.tsv"
REPORTER = ROOT / "scripts/m219-compat-report.py"
PILOT = ROOT / "scripts/probe-m219-alpine-pilot.sh"


def run(*args: str, timeout: int = 5) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=ROOT, capture_output=True, text=True,
                          timeout=timeout, check=False)


def text_oracle(path: Path, user: str) -> bool:
    return run("sh", str(WORKLOAD), "--verify-text", str(path), user).returncode == 0


def sample(user: str = "naseed", *, pid: str = "21942", ident: str = "T219",
           line: str = "ttyS1", host: str = "alpine.test",
           addr: str = "192.0.2.55", date: str = "2023-11-14",
           time: str = "22:13:20", kind: str = "7") -> str:
    return (f"[{kind}] [{pid}] [{ident}] [{user:<8}] [{line:<12}] "
            f"[{host:<20}] [{addr:<15}] [{date}T{time},000000+00:00]\n")


def check_text_cases(directory: Path) -> None:
    dump = directory / "record.txt"
    dump.write_text(sample())
    assert text_oracle(dump, "naseed"), "valid source record rejected"
    dump.write_text(sample("changed"))
    assert text_oracle(dump, "changed"), "valid edited record rejected"
    bad = {
        "empty": "",
        "banner": "utmpdump from util-linux\n",
        "double record": sample() * 2,
        "wrong pid": sample(pid="21943"),
        "wrong type": sample(kind="6"),
        "wrong id": sample(ident="T218"),
        "wrong tty": sample(line="ttyS2"),
        "wrong host": sample(host="alpine.invalid"),
        "wrong address": sample(addr="192.0.2.56"),
        "wrong day": sample(date="2023-11-15"),
        "wrong time": sample(time="22:13:21"),
        "wrong user": sample(user="impersonated"),
        "trailing banner": sample() + "end of dump\n",
    }
    for label, content in bad.items():
        dump.write_text(content)
        assert not text_oracle(dump, "naseed"), (
            f"false original utmp decoded-field PASS: {label}")


def check_native_fixture(directory: Path) -> None:
    cc = shutil.which("cc") or shutil.which("gcc")
    if cc is None:
        raise RuntimeError("M219 host preflight requires a native C compiler")
    helper = directory / "utmp-fixture"
    compiled = run(cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   str(SOURCE), "-o", str(helper))
    assert compiled.returncode == 0, compiled.stderr
    original = directory / "fixture.utmp"
    created = run(str(helper), "--write", str(original))
    assert created.returncode == 0, created.stderr
    assert "M219_UTMP_RECORD_BYTES=" in created.stdout
    assert run(str(helper), "--verify-original", str(original)).returncode == 0
    assert run(str(helper), "--verify-edited", str(original)).returncode != 0
    data = original.read_bytes()
    assert len(data) == 400, "Alpine musl 64-bit utmp wire size must not be glibc384"
    bad = {
        "truncated": data[:-1],
        "extra byte": data + b"\x00",
        "mutated type": bytes([data[0] ^ 1]) + data[1:],
        "mutated pid": data[:4] + bytes([data[4] ^ 1]) + data[5:],
        "mutated user": data.replace(b"naseed", b"change", 1),
    }
    assert bad["mutated user"] != data
    for label, payload in bad.items():
        original.write_bytes(payload)
        assert run(str(helper), "--verify-original", str(original)).returncode != 0, (
            f"false native struct utmp byte PASS: {label}")
    original.write_bytes(data)
    assert run(str(helper), "--verify-original", str(original)).returncode == 0

    # Host utmpdump may be built against glibc's 384-byte x86_64 layout.
    # It is NOT an oracle for Alpine-musl's 400-byte x86_64/RV64/ARM64 ABI.
    # Only real signed Alpine utmpdump inside each native QEMU can validate
    # both decoded semantics and byte-for-byte -r reconstruction.


def check_strict_guest_report(directory: Path) -> None:
    """Reject preinstalled, nonfunctional and fake second QEMU evidence."""
    first = directory / "m219-first-uart.log"
    reopen = directory / "m219-reopen-uart.log"
    report_file = directory / "m219-report.json"
    prefix = "M219_RESULT\tutil-linux-misc\tusr/bin/utmpdump\t"
    first_good = (prefix + "install\tPASS\n" + prefix + "functional\tPASS\n"
                  + "M219_FIRST_DISCOVERY_COMPLETE\n")
    reopen_good = prefix + "reopen\tPASS\nM219_REOPEN_DISCOVERY_COMPLETE\n"

    def check(first_data: str, reopen_data: str) -> subprocess.CompletedProcess[str]:
        first.write_text(first_data)
        reopen.write_text(reopen_data)
        return run("python3", str(REPORTER), str(MANIFEST), str(first),
                   str(reopen), str(report_file), "--require-all")

    good = check(first_good, reopen_good)
    assert good.returncode == 0, good.stderr
    report = json.loads(report_file.read_text())
    assert report["newly_installed_full_pass_entrypoints"] == 1
    assert report["preexisting"] == 0
    assert report["milestone"] == "M219"
    assert report["acceptance"] == "strict-native-first-and-reopen-1-of-1"
    bad = {
        "preinstalled": (first_good.replace("install\tPASS", "install\tPREEXISTING"), reopen_good),
        "function fail": (first_good.replace("functional\tPASS", "functional\tFAIL"), reopen_good),
        "reopen fail": (first_good, reopen_good.replace("reopen\tPASS", "reopen\tFAIL")),
        "missing second boot": (first_good, prefix + "reopen\tPASS\n"),
        "wrong milestone": (first_good.replace("M219_", "M215_"), reopen_good),
    }
    for label, (first_bad, reopen_bad) in bad.items():
        assert check(first_bad, reopen_bad).returncode != 0, (
            f"false M219 two-QEMU guest report accepted: {label}")


def main() -> None:
    workload = WORKLOAD.read_text()
    assert "/usr/bin/utmpdump" in workload and "7f454c46" in workload
    assert 'fixture=/root/m219-fixtures/utmpdump-fixture' in workload
    assert 'original signed utmpdump is not executable inside installed guest' in workload
    assert 'host-built musl-ABI fixture is not executable inside guest' in workload
    assert "--exercise" in workload and "utmp-record-roundtrip" in workload
    assert "utmpdump -r" in workload
    assert "--verify-original" in workload and "--verify-edited" in workload
    assert "--verify-text" in workload and "2023-11-14" in workload
    assert "M219 genuine guest semantic PASS" in workload
    assert "--help" not in workload and "--version" not in workload
    assert "#include <utmp.h>" in SOURCE.read_text()
    assert "musl" in SOURCE.read_text()
    assert "sizeof(struct m219_musl_utmp) == 400" in SOURCE.read_text()
    assert "offsetof(struct m219_musl_utmp, ut_tv) == 344" in SOURCE.read_text()
    assert "ut_tv.tv_sec" in SOURCE.read_text()
    assert "memcmp(&actual, &expected, sizeof(actual))" in SOURCE.read_text()
    assert run("sh", "-n", str(WORKLOAD)).returncode == 0
    assert MANIFEST.read_text().splitlines()[-1] == (
        "util-linux-misc\tusr/bin/utmpdump\tterminal\tutmp-record-roundtrip")
    owner = OWNER.read_text()
    assert "0:0:0:0:1:0)" in owner and "milestone=M219" in owner
    for marker in ("manifest=/root/m219-cohort.tsv",
                   "exercise_script=/root/m219-utmpdump-workload.sh",
                   "first=/root/m219-first-results.tsv",
                   "reopen=/root/m219-reopen-results.tsv",
                   'export NAS_M219_BOOT_PHASE="$phase"'):
        assert marker in owner, f"M219 guest owner missing {marker}"
    runner = RUNNER.read_text()
    for marker in ("tests/m219-utmpdump-workload.sh",
                   '"$overlay/root/m219-utmpdump-workload.sh"',
                   "tests/m219-utmpdump-fixture.c",
                   '"$overlay/root/m219-fixtures/utmpdump-fixture"',
                   "scripts/m219-candidate-elfs.tsv",
                   '"$overlay/root/m219-cohort.tsv"',
                   "scripts/m219-alpine-reopen-cases.tsv",
                   "M219_REOPEN_DISCOVERY_COMPLETE",
                   "scripts/m219-compat-report.py",
                   "scripts/m219-alpine-disk-check.py",
                   "--require-all"):
        assert marker in runner, f"M219 two-QEMU or fixture gate missing {marker}"
    assert "export NAS_M219_PILOT=1" in PILOT.read_text()
    assert run("sh", "-n", str(PILOT)).returncode == 0
    assert run("sh", "-n", str(RUNNER)).returncode == 0
    assert run("sh", "-n", str(OWNER)).returncode == 0
    with tempfile.TemporaryDirectory(prefix="naseed-m219-host-") as tmp:
        path = Path(tmp)
        check_strict_guest_report(path)
        check_text_cases(path)
        check_native_fixture(path)
    print("[M219 HOST PASS] 13 false-positive text cases, native ABI binary "
          "negative cases, strict original decode/reverse/edited-workload "
          "contract (NO native QEMU success claimed)")


if __name__ == "__main__":
    main()
