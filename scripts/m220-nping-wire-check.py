#!/usr/bin/env python3
"""Require the original guest's exact UDP in two independent host/wire oracles.

The existing M196 QEMU Ethernet PCAP reader owns framing. This verifier
accepts no guest CLI success strings: it requires that a *host-side* UDP
socket independently received the nonce and that QEMU captured the same
guest-origin Ethernet UDP datagram on the expected port/address.
"""
from importlib.util import module_from_spec, spec_from_file_location
import ipaddress
import json
from pathlib import Path
import re
import struct
import sys


source = Path(__file__).with_name("m196-public-wire-check.py")
spec = spec_from_file_location("m220_m196_pcap", source)
if spec is None or spec.loader is None:
    raise RuntimeError("M220 requires the independent M196 QEMU PCAP reader")
pcap = module_from_spec(spec)
spec.loader.exec_module(pcap)

EXPECTED = {"first": b"NASEED_NPING_FIRST", "reopen": b"NASEED_NPING_AGAIN"}
GUEST = ipaddress.IPv4Network("10.0.2.0/24")
HOST = ipaddress.IPv4Address("10.0.2.2")
PORT = 15416


def wire_packets(raw: bytes, stage: str) -> list[str]:
    """Return guest IPs sending exact UDP payload; ignore all fake traffic."""
    payload_expected = EXPECTED[stage]
    guest_sources = []
    for frame in pcap.pcap_frames(raw):
        packet = pcap.ipv4_packet(frame)
        if packet is None:
            continue
        src, dst, proto, body = packet
        if proto != 17 or dst != HOST or src not in GUEST or src == HOST:
            continue
        if len(body) < 8:
            continue
        _sport, dport, udp_size, _cksum = struct.unpack_from("!HHHH", body)
        if dport != PORT or udp_size != len(body) or body[8:] != payload_expected:
            continue
        guest_sources.append(str(src))
    return guest_sources


def verify(stage: str, capture: bytes, host_log: str) -> dict:
    if stage not in EXPECTED:
        raise ValueError("invalid independent QEMU boot phase")
    if len(EXPECTED[stage]) != 18 or EXPECTED["first"] == EXPECTED["reopen"]:
        raise ValueError("M220 phase payloads are not distinct equal-length probes")
    if host_log.count(f"[M220 HOST READY] phase={stage} port={PORT}\n") != 1:
        raise ValueError("independent local UDP host socket not ready exactly once")
    recv = [line for line in host_log.splitlines() if line.startswith("[M220 HOST UDP RECV]")]
    expected = re.compile(
        rf"^\[M220 HOST UDP RECV\] phase={stage} "
        rf"payload={EXPECTED[stage].decode('ascii')} peer=127\.0\.0\.1:[0-9]+$")
    if len(recv) != 1 or not expected.fullmatch(recv[0]):
        raise ValueError("host did not independently receive exact once-only phase payload")
    if f"[M220 HOST UDP REJECT] phase={stage}" in host_log:
        raise ValueError("host oracle received unexpected or malformed payload")
    matching = wire_packets(capture, stage)
    if len(matching) != 1:
        raise ValueError(f"expected one genuine QEMU guest Ethernet UDP packet, got {len(matching)}")
    return {"stage": stage, "payload_ascii": EXPECTED[stage].decode("ascii"),
            "host_received": 1, "qemu_pcap_udp_sent": 1,
            "guest_source_ipv4": matching[0], "dst_ipv4": str(HOST),
            "dst_udp_port": PORT}


def main() -> None:
    if len(sys.argv) != 5 or sys.argv[1] not in EXPECTED:
        raise SystemExit("usage: m220-nping-wire-check.py first|reopen QEMU.pcap HOST.log PROOF.json")
    stage = sys.argv[1]
    capture, host_log, proof = (Path(arg) for arg in sys.argv[2:])
    if len({str(p.resolve(strict=False)) for p in (capture, host_log, proof)}) != 3:
        raise SystemExit("[M220 HOST WIRE FAIL] overlapping evidence paths")
    if any(p.is_symlink() for p in (capture, host_log, proof)):
        raise SystemExit("[M220 HOST WIRE FAIL] symlinked evidence paths")
    if proof.exists():
        raise SystemExit("[M220 HOST WIRE FAIL] existing output may be stale")
    try:
        report = verify(stage, capture.read_bytes(), host_log.read_text(errors="replace"))
    except (ValueError, OSError) as exc:
        raise SystemExit(f"[M220 HOST WIRE FAIL] {stage}: {exc}") from exc
    proof.write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
    print(f"[M220 HOST+QEMU UDP WIRE PASS] {stage} exact {report['payload_ascii']} "
          f"{report['guest_source_ipv4']} -> {report['dst_ipv4']}:{PORT}; "
          "host socket 1/1 PCAP 1/1")


if __name__ == "__main__":
    main()
