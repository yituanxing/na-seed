#!/usr/bin/env python3
"""M220 independent host+QEMU wire contract negative tests, NOT QEMU PASS."""
from __future__ import annotations

from importlib.util import module_from_spec, spec_from_file_location
import ipaddress
import json
from pathlib import Path
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
ORACLE = ROOT / "scripts/m220-nping-host-oracle.py"
CHECKER = ROOT / "scripts/m220-nping-wire-check.py"
GUEST = ROOT / "tests/m220-nping-workload.sh"
OWNER = ROOT / "tests/m205-software-workload.sh"
RUNNER = ROOT / "scripts/probe-m197-alpine-software-cohort.sh"
PILOT = ROOT / "scripts/probe-m220-alpine-pilot.sh"
INVENTORY = ROOT / "scripts/inventory-m141-signed-apks.sh"
MANIFEST = ROOT / "scripts/m220-candidate-elfs.tsv"
REPORTER = ROOT / "scripts/m220-compat-report.py"
PACKAGE = ROOT / "rootfs/alpine/packages-m220-candidates.txt"
WORKFLOW = ROOT / ".github/workflows/m220-nping-alpine.yml"

spec = spec_from_file_location("m220_checked_pcap", CHECKER)
assert spec and spec.loader
checker = module_from_spec(spec)
spec.loader.exec_module(checker)


def capture(phase: str, *, source: str = "10.0.2.15", dst: str = "10.0.2.2",
            port: int = 15416, payload: bytes | None = None,
            protocol: int = 17, copies: int = 1) -> bytes:
    data = checker.EXPECTED[phase] if payload is None else payload
    udp = struct.pack("!HHHH", 39120, port, len(data) + 8, 0) + data
    ipv4 = struct.pack("!BBHHHBBH4s4s", 0x45, 0, len(udp) + 20, 15, 0,
                       64, protocol, 0, ipaddress.IPv4Address(source).packed,
                       ipaddress.IPv4Address(dst).packed)
    ethernet = b"\xaa" * 6 + b"\xbb" * 6 + b"\x08\x00" + ipv4 + udp
    # Genuine pcap global header, linktype Ethernet and one exact record.
    header = struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1)
    record = struct.pack("<IIII", 0, 0, len(ethernet), len(ethernet)) + ethernet
    return header + record * copies


def host_log(phase: str, *, payload: str | None = None,
             include_receive: bool = True) -> str:
    marker = f"[M220 HOST READY] phase={phase} port=15416\n"
    if include_receive:
        marker += ("[M220 HOST UDP RECV] "
                   f"phase={phase} payload={payload or checker.EXPECTED[phase].decode()} "
                   "peer=127.0.0.1:39120\n")
    return marker


def verify_negative_cases() -> None:
    for phase in ("first", "reopen"):
        good = checker.verify(phase, capture(phase), host_log(phase))
        assert good["stage"] == phase and good["host_received"] == 1
        assert good["qemu_pcap_udp_sent"] == 1
        scenarios = {
            "CLI-only host success": (capture(phase), host_log(phase, include_receive=False)),
            "fake host match and no packet": (capture(phase, copies=0), host_log(phase)),
            "wrong exact payload": (capture(phase, payload=b"NASEED_NPING_WRONG"), host_log(phase)),
            "wrong phase payload": (capture("reopen" if phase == "first" else "first"), host_log(phase)),
            "wrong UDP port": (capture(phase, port=15417), host_log(phase)),
            "wrong host IP": (capture(phase, dst="10.0.2.3"), host_log(phase)),
            "fake host-origin packet": (capture(phase, source="10.0.2.2"), host_log(phase)),
            "invalid outside guest net": (capture(phase, source="198.51.100.10"), host_log(phase)),
            "not a UDP packet": (capture(phase, protocol=6), host_log(phase)),
            "duplicate host wire UDP": (capture(phase, copies=2), host_log(phase)),
            "truncated PCAP": (capture(phase)[:-1], host_log(phase)),
            "wrong host phase": (capture(phase), host_log("reopen" if phase == "first" else "first")),
            "wrong host payload": (capture(phase), host_log(phase, payload="NASEED_NPING_WRONG")),
        }
        for label, (raw, log) in scenarios.items():
            try:
                checker.verify(phase, raw, log)
            except ValueError:
                pass
            else:
                raise AssertionError(f"false M220 host+PCAP UDP proof accepted: {phase} {label}")


def strict_guest_report(tmp: Path) -> None:
    first = tmp / "first.serial"
    reopen = tmp / "reopen.serial"
    result = tmp / "guest-ledger.json"
    prefix = "M220_RESULT\tnmap-nping\tusr/bin/nping\t"
    first_good = (prefix + "install\tPASS\n" + prefix + "functional\tPASS\n"
                  + "M220_FIRST_DISCOVERY_COMPLETE\n")
    reopen_good = prefix + "reopen\tPASS\nM220_REOPEN_DISCOVERY_COMPLETE\n"

    def check(first_text: str, reopen_text: str) -> subprocess.CompletedProcess[str]:
        first.write_text(first_text)
        reopen.write_text(reopen_text)
        return subprocess.run(["python3", str(REPORTER), str(MANIFEST),
                               str(first), str(reopen), str(result), "--require-all"],
                              cwd=ROOT, text=True, capture_output=True, timeout=5)

    good = check(first_good, reopen_good)
    assert good.returncode == 0, good.stderr
    ledger = json.loads(result.read_text())
    assert ledger["newly_installed_full_pass_entrypoints"] == 1
    assert ledger["preexisting"] == 0
    assert ledger["acceptance"] == "cli-ledger-only-host-wire-gate-required"
    for name, fst, snd in (
        ("preinstalled", first_good.replace("install\tPASS", "install\tPREEXISTING"), reopen_good),
        ("not sent", first_good.replace("functional\tPASS", "functional\tFAIL"), reopen_good),
        ("reopen fail", first_good, reopen_good.replace("reopen\tPASS", "reopen\tFAIL")),
        ("missing reboot", first_good, prefix + "reopen\tPASS\n"),
        ("wrong milestone", first_good.replace("M220", "M219"), reopen_good),
    ):
        assert check(fst, snd).returncode != 0, f"false guest ledger accepted: {name}"


def integration_contract() -> None:
    assert len(checker.EXPECTED["first"]) == len(checker.EXPECTED["reopen"]) == 18
    assert checker.EXPECTED["first"] != checker.EXPECTED["reopen"]
    assert PACKAGE.read_text().splitlines()[-1] == "nmap-nping"
    assert MANIFEST.read_text().splitlines()[-1] == (
        "nmap-nping\tusr/bin/nping\tnetwork\toriginal-udp-wire-delivery")
    guest = GUEST.read_text()
    for marker in ("/usr/bin/nping --unprivileged --udp -v3 -d5",
                   "NASEED_NPING_FIRST", "NASEED_NPING_AGAIN",
                   "/proc/net/naseed-resolv.conf", '"$waited" -ge 90',
                   "[M220 ORIGINAL NPING CLI ONLY]", "7f454c46",
                   "apk info -W /usr/bin/nping"):
        assert marker in guest, f"guest missing {marker}"
    assert "--no-capture" not in guest.split('rc=0\n', 1)[-1]
    assert "--help" not in guest and "--version" not in guest
    owner = OWNER.read_text()
    for marker in ("0:0:0:0:0:1:0)", "milestone=M220",
                   "manifest=/root/m220-cohort.tsv",
                   "exercise_script=/root/m220-nping-workload.sh",
                   "first=/root/m220-first-results.tsv",
                   "reopen=/root/m220-reopen-results.tsv",
                   'export NAS_M220_BOOT_PHASE="$phase"',
                   '"$milestone" = M220 ] && exercise_budget=140'):
        assert marker in owner, f"guest owner missing {marker}"
    runner = RUNNER.read_text()
    for marker in ("m220=${NAS_M220_PILOT:-0}",
                   "tests/m220-nping-workload.sh",
                   '"$overlay/root/m220-nping-workload.sh"',
                   "scripts/m220-candidate-elfs.tsv",
                   '"$overlay/root/m220-cohort.tsv"',
                   "m220_start_host first", "m220_stop_host",
                   "m220_start_host reopen", "scripts/m220-nping-wire-check.py first",
                   "scripts/m220-nping-wire-check.py reopen",
                   "m220-reopen-wire.pcap", "m220-first-host-udp.log",
                   "m220-reopen-host-udp.log", "m220-first-wire.json",
                   "m220-reopen-wire.json", "NAS_VIRTIO_NET_PCAP",
                   "m220-alpine-reopen-cases.tsv", "M220_REOPEN_DISCOVERY_COMPLETE",
                   "scripts/m220-compat-report.py", "scripts/m220-alpine-disk-check.py",
                   "M220 ALPINE ORIGINAL NPING WIRE PASS"):
        assert marker in runner, f"host two-QEMU gate missing {marker}"
    assert "export NAS_M220_PILOT=1" in PILOT.read_text()
    assert "m220" in INVENTORY.read_text()
    assert "[M220 HOST UDP RECV]" in ORACLE.read_text()
    workflow = WORKFLOW.read_text()
    assert "  workflow_dispatch:" in workflow and "  push:" in workflow
    assert "    branches: [naseed-app-batch2]" in workflow
    assert "      - '.github/workflows/m220-nping-alpine.yml'" in workflow
    assert "      - 'tests/m205-software-workload.sh'" not in workflow
    assert "needs: [original-nping-amd64]" in workflow
    for marker in ("m220-first-host-udp.log", "m220-reopen-host-udp.log",
                   "m220-first-wire.json", "m220-reopen-wire.json",
                   "m220-reopen-wire.pcap", "m197-public-wire.pcap"):
        assert workflow.count(marker) == 2, f"native per-ISA artifact missing {marker}"
    for file in (GUEST, OWNER, RUNNER, PILOT, INVENTORY):
        passed = subprocess.run(["sh", "-n", str(file)], cwd=ROOT,
                                capture_output=True, text=True, timeout=5)
        assert passed.returncode == 0, (str(file), passed.stderr)
    validated = subprocess.run(["python3", "scripts/check-m141-candidates.py",
                                "--profile", "m220"], cwd=ROOT,
                               capture_output=True, text=True, timeout=5)
    assert validated.returncode == 0, validated.stderr


def main() -> None:
    verify_negative_cases()
    integration_contract()
    with tempfile.TemporaryDirectory(prefix="naseed-m220-host-") as folder:
        strict_guest_report(Path(folder))
    print("[M220 HOST PASS] 26 independent host/PCAP false-positive negatives, "
          "strict signed guest ledger and two-native-QEMU contract "
          "(NO QEMU acceptance claimed)")


if __name__ == "__main__":
    main()
