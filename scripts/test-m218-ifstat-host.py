#!/usr/bin/env python3
"""M218 parser/negative contract; no guest QEMU or original-ELF PASS claim."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
WORKLOAD = ROOT / "tests/m218-ifstat-workload.sh"
M213 = ROOT / "tests/m213-software-workload.sh"
OWNER = ROOT / "tests/m205-software-workload.sh"
RUNNER = ROOT / "scripts/probe-m197-alpine-software-cohort.sh"
PILOT = ROOT / "scripts/probe-m218-alpine-pilot.sh"
MANIFEST = ROOT / "scripts/m218-candidate-elfs.tsv"
REPORTER = ROOT / "scripts/m218-compat-report.py"
TRAFFIC_MARKER = "M205_REAL_UDP_SENT=40 dst=10.0.2.3:53\n"


def proc(rx_packets: int, tx_packets: int, rx_bytes: int,
         tx_bytes: int) -> str:
    fields = [rx_bytes, rx_packets, 0, 0, 0, 0, 0, 0,
              tx_bytes, tx_packets, 0, 0, 0, 0, 0, 0]
    return ("Inter-| Receive | Transmit\n"
            " face |bytes packets errs drop fifo frame compressed multicast|"
            "bytes packets errs drop fifo colls carrier compressed\n"
            "  eth0: " + " ".join(map(str, fields)) + "\n")


def original_ifstat(rx_packets: int, tx_packets: int, rx_bytes: int,
                    tx_bytes: int, *, source: str = "kernel",
                    name: str = "eth0") -> str:
    # The actual iproute2 -a -s -z -j representation, not a free-form table.
    return json.dumps({source: {name: {
        "rx_packets": rx_packets, "tx_packets": tx_packets,
        "rx_bytes": rx_bytes, "tx_bytes": tx_bytes,
        "rx_errors": 0, "tx_errors": 0,
    }}}, indent=2) + "\n"


def run_case(folder: Path, values: dict[str, str]) -> subprocess.CompletedProcess[str]:
    paths: list[str] = []
    for name in ("proc-before", "ifstat-before", "traffic", "ifstat-after",
                 "proc-after"):
        path = folder / name
        path.write_text(values[name])
        paths.append(str(path))
    return subprocess.run(["sh", str(WORKLOAD), "--verify-samples", *paths],
                          cwd=ROOT, capture_output=True, text=True,
                          timeout=5, check=False)


def strict_report_contract(folder: Path) -> None:
    """M218 cannot inherit a guest PASS from M215 or a missing second VM."""
    first = folder / "first-uart.log"
    reopen = folder / "reopen-uart.log"
    output = folder / "m218-report.json"
    line = "M218_RESULT\tiproute2\tsbin/ifstat\t"
    first_good = (line + "install\tPASS\n" + line + "functional\tPASS\n"
                  + "M218_FIRST_DISCOVERY_COMPLETE\n")
    reopen_good = line + "reopen\tPASS\nM218_REOPEN_DISCOVERY_COMPLETE\n"

    def run(first_text: str, reopen_text: str) -> subprocess.CompletedProcess[str]:
        first.write_text(first_text)
        reopen.write_text(reopen_text)
        return subprocess.run(["python3", str(REPORTER), str(MANIFEST),
                               str(first), str(reopen), str(output), "--require-all"],
                              cwd=ROOT, capture_output=True, text=True, timeout=5)

    passed = run(first_good, reopen_good)
    assert passed.returncode == 0, passed.stderr
    report = json.loads(output.read_text())
    assert report["newly_installed_full_pass_entrypoints"] == 1
    assert report["preexisting"] == 0
    assert report["acceptance"] == "strict-native-first-and-reopen-1-of-1"
    for label, first_bad, reopen_bad in (
        ("preexisting", first_good.replace("install\tPASS", "install\tPREEXISTING"), reopen_good),
        ("functional failure", first_good.replace("functional\tPASS", "functional\tFAIL"), reopen_good),
        ("missing independent reopen", first_good, line + "reopen\tPASS\n"),
        ("reopen failure", first_good, reopen_good.replace("reopen\tPASS", "reopen\tFAIL")),
        ("wrong milestone", first_good.replace("M218_", "M215_"), reopen_good),
    ):
        rejected = run(first_bad, reopen_bad)
        assert rejected.returncode != 0, f"false M218 two-QEMU PASS: {label}"


def main() -> None:
    text = WORKLOAD.read_text()
    assert "--exercise" in text and "--verify-samples" in text
    assert "test -x /sbin/ifstat" in text and "7f454c46" in text
    assert "/sbin/ifstat -a -s -z -j eth0" in text
    assert text.count("/sbin/ifstat -a -s -z -j eth0") == 2
    assert "/proc/net/dev" in text and "/root/m205-fixtures/udp-traffic" in text
    # A second native QEMU has no first-boot UART DHCP wait. Reject future
    # regressions that send all 40 packets before real lwIP route readiness.
    assert '/proc/net/naseed-resolv.conf' in text
    assert 'waited=0' in text and '[ "$waited" -ge 90 ]' in text
    assert 'M218 DHCP ROUTE TIMEOUT' in text
    assert 'M218 GENUINE DHCP ROUTE READY' in text
    assert 'exercise_budget=165' in OWNER.read_text()
    assert "M205_REAL_UDP_SENT=40" in text and "c[2] - b[2] < 40" in text
    assert "M218 genuine guest semantic PASS" in text
    assert "--help" not in text and "--version" not in text
    # Preserve the already proven M215 five + fifteen blocked M213 baseline.
    assert "[M213 BLOCKED] requires staged real UDP stimulus" in M213.read_text()
    assert MANIFEST.read_text().splitlines()[-1] == (
        "iproute2\tsbin/ifstat\tnetwork\tlive-interface-counter-delta")

    owner = OWNER.read_text()
    assert "0:0:0:1:0:0)" in owner and "milestone=M218" in owner
    for marker in ("manifest=/root/m218-cohort.tsv",
                   "exercise_script=/root/m218-ifstat-workload.sh",
                   "first=/root/m218-first-results.tsv",
                   "reopen=/root/m218-reopen-results.tsv"):
        assert marker in owner, f"M218 owner missing: {marker}"
    runner = RUNNER.read_text()
    assert "m218=${NAS_M218_PILOT:-0}" in runner
    for marker in ("tests/m218-ifstat-workload.sh",
                   '"$overlay/root/m218-ifstat-workload.sh"',
                   "scripts/m218-candidate-elfs.tsv",
                   '"$overlay/root/m218-cohort.tsv"',
                   "scripts/m218-alpine-reopen-cases.tsv",
                   "M218_REOPEN_DISCOVERY_COMPLETE",
                   "scripts/m218-compat-report.py",
                   "--require-all",
                   "scripts/m218-alpine-disk-check.py"):
        assert marker in runner, f"M218 two-boot gate missing: {marker}"
    assert ('if [ "$m205" = 1 ] || [ "$m218" = 1 ]; then' in runner
            and 'tests/m205-udp-traffic.c -o "$fixture_udp"' in runner
            and 'sudo install -m 0755 "$fixture_udp"' in runner
            and '"$overlay/root/m205-fixtures/udp-traffic"' in runner
            and 'sudo test -x "$overlay/root/m205-fixtures/udp-traffic"' in runner), (
        "M218 must build and stage a target-native 40-packet UDP fixture")
    assert "export NAS_M218_PILOT=1" in PILOT.read_text()
    subprocess.run(["sh", "-n", str(WORKLOAD)], cwd=ROOT,
                   check=True, timeout=5)

    good = {
        "proc-before": proc(99, 199, 9900, 19900),
        "ifstat-before": original_ifstat(100, 200, 10000, 20000),
        "traffic": TRAFFIC_MARKER,
        "ifstat-after": original_ifstat(110, 244, 10700, 23300),
        "proc-after": proc(111, 245, 10800, 23400),
    }
    with tempfile.TemporaryDirectory(prefix="m218-ifstat-host-") as tmp:
        folder = Path(tmp)
        strict_report_contract(folder)
        passed = run_case(folder, good)
        assert passed.returncode == 0, (
            f"valid bracketed sample rejected: {passed.stdout} {passed.stderr}")
        assert "SAMPLE CONTRACT PASS" in passed.stdout
        assert "genuine guest semantic PASS" not in passed.stdout

        bad: dict[str, dict[str, str]] = {
            "no traffic": {"traffic": "M205_REAL_UDP_SENT=0 dst=10.0.2.3:53\n"},
            "stale ifstat counters": {"ifstat-after": good["ifstat-before"]},
            "fewer than 40 sends observed": {
                "ifstat-after": original_ifstat(110, 239, 10700, 23300)},
            "no physical wire growth": {"proc-after": good["proc-before"]},
            "ifstat outside /proc bracket": {
                "ifstat-after": original_ifstat(110, 246, 10700, 23300)},
            "ifstat before /proc baseline": {
                "ifstat-before": original_ifstat(98, 198, 9800, 19800)},
            "no byte growth": {
                "ifstat-after": original_ifstat(110, 244, 10700, 20000)},
            "wrong counter provenance": {
                "ifstat-after": original_ifstat(110, 244, 10700, 23300,
                                                 source="historical")},
            "empty ifstat provenance": {
                "ifstat-after": original_ifstat(110, 244, 10700, 23300,
                                                 source="")},
            "wrong interface": {
                "ifstat-after": original_ifstat(110, 244, 10700, 23300,
                                                 name="lo")},
            "missing JSON counter": {
                "ifstat-after": good["ifstat-after"].replace(
                    '"tx_bytes": 23300,', '"missing_tx_bytes": 23300,')},
            "duplicate interface": {
                "proc-after": good["proc-after"] +
                              good["proc-after"].splitlines()[-1] + "\n"},
            "non-number JSON": {
                "ifstat-after": good["ifstat-after"].replace(
                    '"tx_packets": 244,', '"tx_packets": "244",')},
        }
        for label, changes in bad.items():
            result = run_case(folder, good | changes)
            assert result.returncode != 0, f"false PASS: {label}"
            assert "genuine guest semantic PASS" not in result.stdout

        for argv in (("--exercise", "bogus", "sbin/ifstat"),
                     ("--exercise", "live-interface-counter-delta",
                      "sbin/bridge")):
            result = subprocess.run(["sh", str(WORKLOAD), *argv], cwd=ROOT,
                                    capture_output=True, text=True, timeout=5)
            assert result.returncode == 2, f"invalid original ELF accepted: {argv}"

    print("[M218 HOST CONTRACT PASS] original ifstat RTM_GETLINK JSON and real "
          "/proc/net/dev TX packet/byte delta, one positive parser fixture, "
          f"{len(bad)} fail-closed negatives, wrong-ELF rejection")
    print("[M218 NOT GUEST ACCEPTED] original Alpine signed ELF, real UDP "
          "wire and first/reopen native QEMU still required")


if __name__ == "__main__":
    main()
