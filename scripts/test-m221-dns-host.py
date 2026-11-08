#!/usr/bin/env python3
"""Fail-closed host/PCAP/guest ledger tests; NEVER claims QEMU passed."""
from importlib.util import module_from_spec, spec_from_file_location
import ipaddress
import json
from pathlib import Path
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
CHECKER = ROOT / "scripts/m221-dns-wire-check.py"
GUEST = ROOT / "tests/m221-dns-update-workload.sh"
OWNER = ROOT / "tests/m205-software-workload.sh"
RUNNER = ROOT / "scripts/probe-m197-alpine-software-cohort.sh"
PILOT = ROOT / "scripts/probe-m221-alpine-pilot.sh"
MANIFEST = ROOT / "scripts/m221-candidate-elfs.tsv"
PACKAGES = ROOT / "rootfs/alpine/packages-m221-candidates.txt"
REPORTER = ROOT / "scripts/m221-compat-report.py"
WORKFLOW = ROOT / ".github/workflows/m221-dns-update-alpine.yml"

spec = spec_from_file_location("m221_host_checked", CHECKER)
assert spec and spec.loader
checker = module_from_spec(spec)
spec.loader.exec_module(checker)


def dns_name(value: str) -> bytes:
    assert value.endswith(".")
    return b"".join(bytes((len(part),)) + part.encode("ascii")
                    for part in value[:-1].split(".")) + b"\x00"


def dns_messages(stage: str) -> dict[str, bytes]:
    owner, address = checker.server.PHASES[stage]
    question = dns_name("naseed.test.") + struct.pack("!HH", 6, 1)
    record = (dns_name(owner) + struct.pack("!HHIH", 1, 1, 60, 4) +
              ipaddress.IPv4Address(address).packed)
    update = struct.pack("!HHHHHH", 0x2211, 5 << 11, 1, 0, 1, 0) + question + record
    query = (struct.pack("!HHHHHH", 0x2212, 0x0100, 1, 0, 0, 0) +
             dns_name(owner) + struct.pack("!HH", 1, 1))
    checker.server.dns.OWNER = owner
    checker.server.dns.ADDRESS = address
    oracle = checker.server.dns.Oracle()
    ack, event = oracle.handle(update)
    assert event == "UPDATE" and ack
    answer, event = oracle.handle(query)
    assert event == "QUERY" and answer
    return {"update": update, "ack": ack, "query": query, "answer": answer}


def ethernet_udp(payload: bytes, *, source: str, destination: str,
                 sport: int, dport: int) -> bytes:
    udp = struct.pack("!HHHH", sport, dport, len(payload) + 8, 0) + payload
    ipv4 = struct.pack("!BBHHHBBH4s4s", 0x45, 0, len(udp) + 20, 221,
                       0, 64, 17, 0, ipaddress.IPv4Address(source).packed,
                       ipaddress.IPv4Address(destination).packed)
    return b"\xaa" * 6 + b"\xbb" * 6 + b"\x08\x00" + ipv4 + udp


def captured(stage: str, *, order: tuple[str, ...] =
             ("update", "ack", "query", "answer"),
             wrong_update: bytes | None = None,
             wrong_query: bytes | None = None,
             wrong_destination: str | None = None,
             wrong_port: int | None = None,
             wrong_source: str | None = None) -> bytes:
    data = dns_messages(stage)
    if wrong_update is not None:
        data["update"] = wrong_update
    if wrong_query is not None:
        data["query"] = wrong_query
    header = struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1)
    for label in order:
        guest = label in ("update", "query")
        frame = ethernet_udp(
            data[label], source=(wrong_source or "10.0.2.15") if guest else "10.0.2.2",
            destination=(wrong_destination or "10.0.2.2") if guest else "10.0.2.15",
            sport=(39001 if label == "update" else 39002) if guest else 15416,
            dport=(wrong_port or 15416) if guest else
                  (39001 if label == "ack" else 39002))
        header += struct.pack("<IIII", 0, 0, len(frame), len(frame)) + frame
    return header


def host_log(stage: str) -> str:
    owner, address = checker.server.PHASES[stage]
    return "\n".join((
        f"[M221 HOST READY] phase={stage} port=15416 owner={owner} ip={address}",
        f"[M221 HOST UPDATE] phase={stage} owner={owner} ip={address} ttl=60",
        f"[M221 HOST ACK] phase={stage} status=RFC2136_NOERROR",
        f"[M221 HOST QUERY] phase={stage} owner={owner} type=A",
        f"[M221 HOST ANSWER] phase={stage} owner={owner} ip={address}",
    )) + "\n"


def reject(stage: str, label: str, raw: bytes, transcript: str) -> None:
    try:
        checker.verify(stage, raw, transcript)
    except ValueError:
        return
    raise AssertionError(f"false-positive RFC2136/PCAP accepted: {stage}: {label}")


def negative_oracles() -> int:
    count = 0
    for stage in ("first", "reopen"):
        good_capture = captured(stage)
        good_log = host_log(stage)
        result = checker.verify(stage, good_capture, good_log)
        assert result["pcap_guest_update_packets"] == 1
        assert result["pcap_guest_query_packets"] == 1
        assert result["pcap_real_responses_verified"] == 2
        # All of these must fail even if original guest command returned 0.
        cases = {
            "host no UPDATE": (good_capture, good_log.replace("[M221 HOST UPDATE]", "[IGNORED]")),
            "host no ACK": (good_capture, good_log.replace("[M221 HOST ACK]", "[IGNORED]")),
            "host no ANSWER": (good_capture, good_log.replace("[M221 HOST ANSWER]", "[IGNORED]")),
            "host log wrong phase": (good_capture, host_log("reopen" if stage == "first" else "first")),
            "host duplicated READY": (good_capture, good_log.splitlines()[0] + "\n" + good_log),
            "host rejected UDP": (good_capture, good_log + f"[M221 HOST REJECT] phase={stage} length=8\n"),
            "host QUERY before ACK": (good_capture, "\n".join(good_log.splitlines()[:2] + good_log.splitlines()[3:4] + good_log.splitlines()[2:3] + good_log.splitlines()[4:]) + "\n"),
            "no guest packets": (captured(stage, order=()), good_log),
            "no guest query": (captured(stage, order=("update", "ack")), good_log),
            "no real ACK": (captured(stage, order=("update", "query", "answer")), good_log),
            "no real query ANSWER": (captured(stage, order=("update", "ack", "query")), good_log),
            "preseeded ACK before UPDATE": (captured(stage, order=("ack", "update", "query", "answer")), good_log),
            "preseeded QUERY reply before QUERY": (captured(stage, order=("update", "ack", "answer", "query")), good_log),
            "readback before authority update": (captured(stage, order=("query", "answer", "update", "ack")), good_log),
            "wrong host IP": (captured(stage, wrong_destination="10.0.2.3"), good_log),
            "fake host-origin requests": (captured(stage, wrong_source="10.0.2.2"), good_log),
            "wrong UDP port": (captured(stage, wrong_port=15417), good_log),
            "another phase packets": (captured("reopen" if stage == "first" else "first"), good_log),
            "bad DNS UPDATE body": (captured(stage, wrong_update=b"not RFC2136"), good_log),
            "bad DNS QUERY body": (captured(stage, wrong_query=b"not a DNS query"), good_log),
            "truncated PCAP": (good_capture[:-1], good_log),
        }
        for label, (raw, log) in cases.items():
            reject(stage, label, raw, log)
            count += 1
    return count


def guest_ledgers(tmp: Path) -> None:
    first, reopen, report = [tmp / name for name in ("first", "reopen", "report.json")]
    paths = ("usr/bin/nsupdate", "usr/bin/host")

    def content(phase: str) -> str:
        states = ("install", "functional") if phase == "first" else ("reopen",)
        return "".join(f"M221_RESULT\tbind-tools\t{path}\t{state}\tPASS\n"
                       for path in paths for state in states) + \
               f"M221_{'FIRST' if phase == 'first' else 'REOPEN'}_DISCOVERY_COMPLETE\n"

    def check(a: str, b: str) -> int:
        first.write_text(a)
        reopen.write_text(b)
        p = subprocess.run(["python3", str(REPORTER), str(MANIFEST),
                            str(first), str(reopen), str(report), "--require-all"],
                           cwd=ROOT, text=True, capture_output=True, timeout=5)
        return p.returncode

    good_first, good_reopen = content("first"), content("reopen")
    assert check(good_first, good_reopen) == 0
    data = json.loads(report.read_text())
    assert data["newly_installed_full_pass_entrypoints"] == 2
    assert data["preexisting"] == 0
    assert data["acceptance"] == "cli-ledger-only-authoritative-host-and-pcap-required"
    for label, a, b in (
        ("preinstalled", good_first.replace("install\tPASS", "install\tPREEXISTING", 1), good_reopen),
        ("functional false", good_first.replace("functional\tPASS", "functional\tFAIL", 1), good_reopen),
        ("missing original host", good_first.replace("usr/bin/host", "usr/bin/replace"), good_reopen),
        ("reopen failed", good_first, good_reopen.replace("reopen\tPASS", "reopen\tFAIL", 1)),
        ("reopen missing", good_first, good_reopen.replace("M221_REOPEN_DISCOVERY_COMPLETE", "")),
    ):
        assert check(a, b) != 0, f"false positive guest ledger {label}"


def integration_contract() -> None:
    assert PACKAGES.read_text().splitlines()[-1] == "bind-tools"
    manifest = [line for line in MANIFEST.read_text().splitlines()
                if line and not line.startswith("#")]
    assert len(manifest) == 3
    assert manifest[1] == "bind-tools\tusr/bin/nsupdate\tdns\trfc2136-authoritative-update"
    assert manifest[2] == "bind-tools\tusr/bin/host\tdns\trfc2136-update-readback"
    guest = GUEST.read_text()
    for marker in ("/usr/bin/nsupdate -4 -t 8 -u 3 -r 2",
                   "/usr/bin/host -4 -t A -p 15416", "zone naseed.test.",
                   "m221-first.naseed.test.", "m221-reopen.naseed.test.",
                   "192.0.2.221", "192.0.2.222", "NAS_M221_BOOT_PHASE",
                   "/proc/net/naseed-resolv.conf", '"$waited" -ge 90'):
        assert marker in guest, f"guest missing {marker}"
    owner = OWNER.read_text()
    for marker in ("0:0:0:0:0:0:1)", "milestone=M221",
                   "manifest=/root/m221-cohort.tsv", "expected_rows=2",
                   "exercise_script=/root/m221-dns-update-workload.sh",
                   'export NAS_M221_BOOT_PHASE="$phase"',
                   '"$milestone" = M221 ] && exercise_budget=140'):
        assert marker in owner, f"M205 strict two-ELF ledger missing {marker}"
    runner = RUNNER.read_text() + '\n' + (ROOT / 'scripts/acceptance/profile.sh').read_text()
    for marker in ("m221=${NAS_M221_PILOT:-0}", "m221_start_host first",
                   "m221_start_host reopen", "m221_stop_host",
                   "scripts/m221-dns-wire-check.py first",
                   "scripts/m221-dns-wire-check.py reopen",
                   "m221-first-host-dns.log", "m221-reopen-host-dns.log",
                   "m221-first-wire.json", "m221-reopen-wire.json",
                   "m221-reopen-wire.pcap", "m221-alpine-reopen-cases.tsv",
                   "M221_FIRST_DISCOVERY_COMPLETE", "M221_REOPEN_DISCOVERY_COMPLETE",
                   "scripts/m221-compat-report.py", "scripts/m221-alpine-disk-check.py"):
        assert marker in runner, f"M197 shared first/reopen gate missing {marker}"
    assert "export NAS_M221_PILOT=1" in PILOT.read_text()
    flow = WORKFLOW.read_text()
    assert "  workflow_dispatch:" in flow and "  push:" in flow
    assert "    branches: [naseed-app-batch2]" in flow
    assert "      - '.github/workflows/m221-dns-update-alpine.yml'" in flow
    assert "      - 'tests/m205-software-workload.sh'" not in flow
    assert "needs: [authoritative-dns-amd64]" in flow
    for marker in ("m221-first-host-dns.log", "m221-reopen-host-dns.log",
                   "m221-first-wire.json", "m221-reopen-wire.json",
                   "m221-reopen-wire.pcap", "m197-public-wire.pcap"):
        assert flow.count(marker) == 2, f"two cross-ISA evidence exports absent {marker}"
    for path in (GUEST, OWNER, RUNNER, PILOT, ROOT / "scripts/inventory-m141-signed-apks.sh"):
        checked = subprocess.run(["sh", "-n", str(path)], cwd=ROOT,
                                 capture_output=True, text=True, timeout=5)
        assert checked.returncode == 0, (path, checked.stderr)
    census = subprocess.run(["python3", "scripts/check-m141-candidates.py",
                             "--profile", "m221"], cwd=ROOT,
                            text=True, capture_output=True, timeout=5)
    assert census.returncode == 0, census.stderr


def main() -> None:
    negative_count = negative_oracles()
    with tempfile.TemporaryDirectory(prefix="naseed-m221-host-") as folder:
        guest_ledgers(Path(folder))
    integration_contract()
    print(f"[M221 HOST PASS] {negative_count} fail-closed host/PCAP negatives, "
          "two genuine original-ELF guest ledger negatives, 3ISA double-QEMU wiring; "
          "NO native signed APK or QEMU acceptance claimed")


if __name__ == "__main__":
    main()
