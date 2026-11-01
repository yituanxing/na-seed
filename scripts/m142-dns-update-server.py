#!/usr/bin/env python3
"""Independent RFC 2136 wire oracle for original Alpine nsupdate + host.

Only an exact unsigned one-record UPDATE for naseed.test. changes state.
The subsequent A query returns the TEST-NET address only after that update.
An optional Nping UDP payload is recognized independently of DNS.
"""

import socket
import struct


PORT = 15416
ZONE = "naseed.test."
OWNER = "updated.naseed.test."
ADDRESS = "192.0.2.99"  # RFC 5737 documentation network
NPING_PAYLOAD = b"NASEED_NPING_PROBE"


def _name(data: bytes, pos: int, seen: frozenset[int] = frozenset()):
    labels = []
    end = None
    for _ in range(128):
        if pos >= len(data) or pos in seen:
            raise ValueError("DNS name outside packet or compression loop")
        seen = seen | {pos}
        length = data[pos]
        if length == 0:
            pos += 1
            return ".".join(labels).lower() + ".", end or pos
        if length & 0xC0 == 0xC0:
            if pos + 1 >= len(data):
                raise ValueError("short compression pointer")
            target = ((length & 0x3F) << 8) | data[pos + 1]
            if target >= pos:
                raise ValueError("forward compression pointer")
            if end is None:
                end = pos + 2
            suffix, _ = _name(data, target, seen)
            return ".".join(labels).lower() + ("." if labels else "") + suffix, end
        if length > 63 or length & 0xC0 or pos + 1 + length > len(data):
            raise ValueError("invalid DNS label")
        try:
            labels.append(data[pos + 1:pos + 1 + length].decode("ascii"))
        except UnicodeDecodeError as exc:
            raise ValueError("non-ASCII DNS label") from exc
        pos += 1 + length
    raise ValueError("too many DNS labels")


def _question(data: bytes, pos: int):
    name, pos = _name(data, pos)
    if pos + 4 > len(data):
        raise ValueError("short DNS question")
    qtype, qclass = struct.unpack_from("!HH", data, pos)
    return name, qtype, qclass, pos + 4


def _record(data: bytes, pos: int):
    name, pos = _name(data, pos)
    if pos + 10 > len(data):
        raise ValueError("short DNS resource record")
    rtype, rclass, ttl, rdlen = struct.unpack_from("!HHIH", data, pos)
    pos += 10
    if pos + rdlen > len(data):
        raise ValueError("truncated DNS RDATA")
    return (name, rtype, rclass, ttl, data[pos:pos + rdlen]), pos + rdlen


class Oracle:
    def __init__(self):
        self.updated = False
        self.updates = 0
        self.queries = 0
        self.nping = 0

    def handle(self, packet: bytes):
        """Return (UDP response or None, accepted event or None)."""
        if packet == NPING_PAYLOAD:
            self.nping += 1
            return None, "NPING"
        if len(packet) < 12:
            return None, None
        try:
            id_, flags, qd, an, ns, ar = struct.unpack_from("!HHHHHH", packet)
            opcode = (flags >> 11) & 15
            if flags & 0x8000 or qd != 1:
                return None, None
            name, qtype, qclass, offset = _question(packet, 12)
            question = packet[12:offset]
            if opcode == 5:  # RFC 2136 UPDATE
                if (name, qtype, qclass) != (ZONE, 6, 1) or an != 0 or ns != 1:
                    return None, None
                updated, offset = _record(packet, offset)
                if updated != (OWNER, 1, 1, 60, socket.inet_aton(ADDRESS)):
                    return None, None
                # Accept either no additional section or one EDNS OPT RR,
                # but never fabricate authenticated TSIG/SIG0 acceptance.
                if ar:
                    if ar != 1:
                        return None, None
                    extra, offset = _record(packet, offset)
                    if extra[0] != "." or extra[1] != 41:
                        return None, None
                if offset != len(packet):
                    return None, None
                self.updated = True
                self.updates += 1
                header = struct.pack("!HHHHHH", id_, 0xA800, 1, 0, 0, 0)
                return header + question, "UPDATE"
            if opcode != 0 or an or ns:
                return None, None
            if ar:
                if ar != 1:
                    return None, None
                extra, offset = _record(packet, offset)
                if extra[0] != "." or extra[1] != 41:
                    return None, None
            if offset != len(packet) or qclass != 1:
                return None, None
            response_flags = 0x8480 | (flags & 0x0100)  # QR, AA, RA, RD
            if (name, qtype) == (OWNER, 1) and self.updated:
                self.queries += 1
                header = struct.pack("!HHHHHH", id_, response_flags, 1, 1, 0, 0)
                record = b"\xc0\x0c" + struct.pack("!HHIH", 1, 1, 60, 4)
                return header + question + record + socket.inet_aton(ADDRESS), "QUERY"
            if (name, qtype) == (ZONE, 6):
                # A bounded authoritative SOA response for clients that
                # discover zone metadata before submitting an explicit UPDATE.
                mname = b"\x02ns\x06naseed\x04test\x00"
                rname = b"\x0ahostmaster\x06naseed\x04test\x00"
                serial = struct.pack("!IIIII", 2026110101, 3600, 900, 604800, 60)
                rdata = mname + rname + serial
                header = struct.pack("!HHHHHH", id_, response_flags, 1, 1, 0, 0)
                record = b"\xc0\x0c" + struct.pack("!HHIH", 6, 1, 60, len(rdata))
                return header + question + record + rdata, "SOA"
            return None, None
        except (ValueError, struct.error):
            return None, None


def main():
    oracle = Oracle()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.bind(("127.0.0.1", PORT))
        print(f"[M142 DNS HOST READY] udp 127.0.0.1:{PORT}", flush=True)
        while True:
            packet, peer = server.recvfrom(4096)
            response, event = oracle.handle(packet)
            if event == "UPDATE":
                print(f"[M142 DNS HOST UPDATE] {ZONE} {OWNER} A {ADDRESS} ttl=60", flush=True)
            elif event == "QUERY":
                print(f"[M142 DNS HOST QUERY] post-update A {OWNER}", flush=True)
            elif event == "NPING":
                print("[M142 DNS HOST NPING] exact unprivileged UDP probe", flush=True)
            elif event == "SOA":
                print("[M142 DNS HOST SOA] zone metadata", flush=True)
            else:
                print("[M142 DNS HOST IGNORE] unrecognized/malformed datagram", flush=True)
            if response is not None:
                server.sendto(response, peer)
                if event == "UPDATE":
                    print("[M142 DNS HOST ACK] RFC2136 NOERROR", flush=True)
                elif event == "QUERY":
                    print(f"[M142 DNS HOST ANSWER] {ADDRESS}", flush=True)


if __name__ == "__main__":
    main()
