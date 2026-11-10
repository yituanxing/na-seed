#!/usr/bin/env python3
"""Independent QEMU-side proof of public DNS and TCP/TLS from Alpine.

Read QEMU's unmodified Ethernet PCAP, not guest success-marker text. A
local 10.0.2.2 HTTP server, a DNS-only exchange, or a TLS-only exchange
must never be mistaken for real Internet package installation.
"""

import ipaddress
import json
from pathlib import Path
import struct
import sys


GUEST_NET = ipaddress.IPv4Network("10.0.2.0/24")
DNS_SERVER = ipaddress.IPv4Address("10.0.2.3")
DOMAIN = "dl-cdn.alpinelinux.org"


def pcap_frames(data: bytes):
    if len(data) < 24:
        raise ValueError("PCAP header missing")
    magic = data[:4]
    if magic in (b"\xd4\xc3\xb2\xa1", b"\x4d\x3c\xb2\xa1"):
        endian = "<"
    elif magic in (b"\xa1\xb2\xc3\xd4", b"\xa1\xb2\x3c\x4d"):
        endian = ">"
    else:
        raise ValueError("not a PCAP capture")
    if struct.unpack_from(endian + "I", data, 20)[0] != 1:
        raise ValueError("expected QEMU Ethernet PCAP")
    offset = 24
    while offset < len(data):
        if offset + 16 > len(data):
            raise ValueError("truncated PCAP record")
        captured = struct.unpack_from(endian + "I", data, offset + 8)[0]
        offset += 16
        if captured > 65535 or offset + captured > len(data):
            raise ValueError("invalid PCAP record size")
        yield data[offset:offset + captured]
        offset += captured


def ipv4_packet(frame: bytes):
    if len(frame) < 34 or frame[12:14] != b"\x08\x00":
        return None
    ip = frame[14:]
    if ip[0] >> 4 != 4:
        return None
    ihl = (ip[0] & 0x0f) * 4
    if ihl < 20 or len(ip) < ihl:
        return None
    total = struct.unpack_from("!H", ip, 2)[0]
    if total < ihl or len(ip) < total:
        return None
    if struct.unpack_from("!H", ip, 6)[0] & 0x1fff:
        return None
    src = ipaddress.IPv4Address(ip[12:16])
    dst = ipaddress.IPv4Address(ip[16:20])
    return src, dst, ip[9], ip[ihl:total]


def dns_question(data: bytes):
    if len(data) < 17:
        return None
    ident, flags, questions, answers, authority, additional = struct.unpack_from(
        "!HHHHHH", data)
    if questions < 1:
        return None
    at = 12
    labels = []
    while at < len(data):
        length = data[at]
        at += 1
        if length == 0:
            break
        if length > 63 or at + length > len(data):
            return None
        labels.append(data[at:at + length].decode("ascii", errors="ignore").lower())
        at += length
    else:
        return None
    if at + 4 > len(data):
        return None
    at += 4  # QTYPE + QCLASS
    public_answers = set()
    for _ in range(answers + authority + additional):
        # DNS answer owner names may use RFC 1035 compression pointers.
        # Only skip these names; the real QNAME is checked independently.
        for _component in range(128):
            if at >= len(data):
                return None
            length = data[at]
            if length & 0xc0 == 0xc0:
                if at + 2 > len(data):
                    return None
                at += 2
                break
            if length & 0xc0:
                return None
            at += 1
            if not length:
                break
            if at + length > len(data):
                return None
            at += length
        else:
            return None
        if at + 10 > len(data):
            return None
        record_type, record_class, _ttl, size = struct.unpack_from("!HHIH", data, at)
        at += 10
        if at + size > len(data):
            return None
        if record_type == 1 and record_class == 1 and size == 4:
            address = ipaddress.IPv4Address(data[at:at + size])
            if address.is_global:
                public_answers.add(str(address))
        at += size
    return ident, flags, answers, ".".join(labels), public_answers


def verify_capture(pcap: bytes):
    queries = set()
    replies = {}
    public_syn = set()
    public_synack = set()
    public_tls = set()
    for frame in pcap_frames(pcap):
        parsed = ipv4_packet(frame)
        if not parsed:
            continue
        src, dst, protocol, payload = parsed
        if protocol == 17 and len(payload) >= 8:
            sport, dport, size = struct.unpack_from("!HHH", payload)
            if size < 8 or size > len(payload):
                continue
            dns = dns_question(payload[8:size])
            if not dns:
                continue
            ident, flags, answers, name, public_answers = dns
            if name != DOMAIN:
                continue
            if src in GUEST_NET and dst == DNS_SERVER and dport == 53 and not flags & 0x8000:
                queries.add((sport, ident))
            elif src == DNS_SERVER and dst in GUEST_NET and sport == 53 and flags & 0x8000 and answers:
                replies.setdefault((dport, ident), set()).update(public_answers)
        if protocol != 6 or len(payload) < 20:
            continue
        sport, dport = struct.unpack_from("!HH", payload)
        hlen = ((payload[12] >> 4) & 15) * 4
        if hlen < 20 or hlen > len(payload):
            continue
        flags = payload[13]
        body = payload[hlen:]
        if src in GUEST_NET and dst.is_global and dport == 443:
            key = (str(src), sport, str(dst), dport)
            if flags & 0x02 and not flags & 0x10:
                public_syn.add(key)
            # TLS record type handshake=22, outer version TLS1.0-1.3,
            # first handshake message type client_hello=1. No payload
            # inspection or certificate bypass; APK verifies HTTPS itself.
            if len(body) >= 6 and body[0] == 22 and body[1] == 3 and body[5] == 1:
                public_tls.add(key)
        elif dst in GUEST_NET and src.is_global and sport == 443:
            key = (str(dst), dport, str(src), sport)
            if flags & 0x12 == 0x12:
                public_synack.add(key)
    matching_dns = queries & replies.keys()
    resolved_public_ips = set()
    for key in matching_dns:
        resolved_public_ips.update(replies[key])
    matching_tcp = {key for key in public_syn & public_synack & public_tls
                    if key[2] in resolved_public_ips}
    if not matching_dns:
        raise ValueError("no matching real QEMU DNS query+answer for official Alpine CDN")
    if not matching_tcp:
        raise ValueError("no CDN-resolved public TCP:443 SYN/SYN-ACK+TLS ClientHello")
    public_endpoints = sorted({entry[2] for entry in matching_tcp})
    return {"dns_queries_replied": len(matching_dns),
            "public_tls_connections": len(matching_tcp),
            "public_endpoints": public_endpoints,
            "domain": DOMAIN}


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: m196-public-wire-check.py CAPTURE.pcap OUTPUT.json")
    pcap = Path(sys.argv[1])
    output = Path(sys.argv[2])
    if pcap.is_symlink() or output.is_symlink():
        raise SystemExit("[M196 PUBLIC WIRE FAIL] symlinked capture/output")
    try:
        result = verify_capture(pcap.read_bytes())
    except (OSError, ValueError) as exc:
        raise SystemExit(f"[M196 PUBLIC WIRE FAIL] {exc}") from exc
    output.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    print(f"[M196 PUBLIC DNS TLS WIRE PASS] DNS={result['dns_queries_replied']} "
          f"public TLS={result['public_tls_connections']} "
          f"endpoints={','.join(result['public_endpoints'])}")


if __name__ == "__main__":
    main()
