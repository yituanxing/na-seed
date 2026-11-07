#!/usr/bin/env python3
"""Two independent RFC2136 DNS UPDATE/QUERY wire + host-socket oracles.

An original `nsupdate` success banner is never acceptance. The host must have
received and ACKed the exact phase-specific update, independently answered
the ensuing original `host` readback, and QEMU's Ethernet PCAP must contain
both causally ordered guest-origin packets and their genuine DNS responses.
"""
from importlib.util import module_from_spec, spec_from_file_location
import ipaddress
import json
from pathlib import Path
import struct
import sys


def load(label: str, name: str):
    path = Path(__file__).with_name(name)
    spec = spec_from_file_location(label, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"M221 cannot load {name}")
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


frames = load("m221_pcap", "m196-public-wire-check.py")
server = load("m221_dns_host", "m221-dns-host-oracle.py")
HOST = ipaddress.IPv4Address("10.0.2.2")
GUEST = ipaddress.IPv4Network("10.0.2.0/24")
PORT = server.PORT


def verify(stage: str, raw: bytes, host_log: str) -> dict:
    if stage not in server.PHASES:
        raise ValueError("unknown native QEMU boot phase")
    owner, address = server.PHASES[stage]
    wanted = (
        f"[M221 HOST READY] phase={stage} port={PORT} owner={owner} ip={address}",
        f"[M221 HOST UPDATE] phase={stage} owner={owner} ip={address} ttl=60",
        f"[M221 HOST ACK] phase={stage} status=RFC2136_NOERROR",
        f"[M221 HOST QUERY] phase={stage} owner={owner} type=A",
        f"[M221 HOST ANSWER] phase={stage} owner={owner} ip={address}",
    )
    lines = host_log.splitlines()
    if lines.count(wanted[0]) != 1 or any(marker not in lines for marker in wanted[1:]):
        raise ValueError("independent authoritative host missing UPDATE, ACK, QUERY or ANSWER")
    if not all(lines.index(wanted[i]) < lines.index(wanted[i + 1])
               for i in range(len(wanted) - 1)):
        raise ValueError("host authoritative update/response/readback order violated")
    if any(line.startswith("[M221 HOST REJECT]") for line in lines):
        raise ValueError("host DNS parser rejected an incoming guest datagram")
    other_phase = "reopen" if stage == "first" else "first"
    if any(f"phase={other_phase}" in line for line in lines):
        raise ValueError("mixed first/reopen host log cannot be accepted")

    server.dns.OWNER = owner
    server.dns.ADDRESS = address
    oracle = server.dns.Oracle()
    # (kind, source-port, exact expected DNS reply, guest IP, captured frame #)
    events = []
    responses = []
    for frame_number, frame in enumerate(frames.pcap_frames(raw)):
        ipv4 = frames.ipv4_packet(frame)
        if ipv4 is None:
            continue
        src, dst, protocol, body = ipv4
        if protocol != 17 or len(body) < 8:
            continue
        sport, dport, length, _checksum = struct.unpack_from("!HHHH", body)
        if length != len(body):
            continue
        payload = body[8:]
        if src in GUEST and src != HOST and dst == HOST and dport == PORT:
            reply, kind = oracle.handle(payload)
            if kind in ("UPDATE", "QUERY") and reply is not None:
                events.append((kind, sport, reply, str(src), frame_number))
        elif src == HOST and dst in GUEST and dst != HOST and sport == PORT:
            responses.append((dport, payload, str(dst), frame_number))

    kinds = [event[0] for event in events]
    if not kinds or kinds[0] != "UPDATE" or "QUERY" not in kinds:
        raise ValueError("PCAP lacks causal RFC2136 UPDATE followed by A readback")
    first_query = kinds.index("QUERY")
    if first_query == 0:
        raise ValueError("DNS readback preceded signed original update")
    matching_pairs = {"UPDATE": [], "QUERY": []}
    for kind, sport, reply, guest, request_frame in events:
        for reply_port, reply_data, destination, reply_frame in responses:
            if (reply_port, reply_data, destination) == (sport, reply, guest) and \
               reply_frame > request_frame:
                matching_pairs[kind].append((request_frame, reply_frame))
    # A previously cached, replayed or reordered response must never count.
    # Require the actual UPDATE request, host ACK, subsequent A readback query
    # and the matching host ANSWER IN THIS ORDER in the same QEMU capture.
    if not any(u_request < u_reply < q_request < q_reply
               for u_request, u_reply in matching_pairs["UPDATE"]
               for q_request, q_reply in matching_pairs["QUERY"]):
        raise ValueError("PCAP lacks causally ordered UPDATE/ACK/QUERY/ANSWER")
    return {
        "phase": stage, "original_nsupdate": "usr/bin/nsupdate",
        "original_host": "usr/bin/host", "owner": owner,
        "address": address, "zone": "naseed.test.",
        "host_authoritative_update_acked": True,
        "host_authoritative_readback_answered": True,
        "pcap_guest_update_packets": kinds.count("UPDATE"),
        "pcap_guest_query_packets": kinds.count("QUERY"),
        "pcap_real_responses_verified": 2,
        "guest_source_ipv4": events[0][3], "dns_port": PORT,
    }


def main() -> None:
    if len(sys.argv) != 5 or sys.argv[1] not in server.PHASES:
        raise SystemExit("usage: m221-dns-wire-check.py first|reopen PCAP HOST_LOG JSON")
    stage = sys.argv[1]
    cap, host_log, report = [Path(arg) for arg in sys.argv[2:]]
    if len({str(path.resolve(strict=False)) for path in (cap, host_log, report)}) != 3:
        raise SystemExit("[M221 WIRE FAIL] overlapping evidence paths")
    if any(path.is_symlink() for path in (cap, host_log, report)) or report.exists():
        raise SystemExit("[M221 WIRE FAIL] stale/symlinked evidence")
    try:
        proof = verify(stage, cap.read_bytes(), host_log.read_text(errors="replace"))
    except (OSError, ValueError) as exc:
        raise SystemExit(f"[M221 WIRE FAIL] {stage}: {exc}") from exc
    report.write_text(json.dumps(proof, indent=2, sort_keys=True) + "\n")
    print(f"[M221 HOST+PCAP RFC2136 PASS] {stage} original nsupdate+host "
          f"update/readback {proof['owner']}={proof['address']}; "
          "genuine host ACK/answer and both PCAP replies")


if __name__ == "__main__":
    main()
