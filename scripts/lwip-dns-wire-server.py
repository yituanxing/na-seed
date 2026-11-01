#!/usr/bin/env python3
"""Deterministic host-only DNS responder for untouched Alpine dig in QEMU.

This is an independent wire oracle, not a guest library or a fake DNS syscall.
Only one IPv4 A query for naseed.test. receives the documented answer.
"""

import socket
import struct


PORT = 15415
DOMAIN = "naseed.test."
ANSWER = "192.0.2.42"  # RFC 5737 TEST-NET-1, never a real host


def answer_query(packet: bytes) -> bytes | None:
    if len(packet) < 17:
        return None
    identifier, flags, questions, answers, authority, additional = struct.unpack(
        "!HHHHHH", packet[:12]
    )
    if flags & 0x8000 or questions != 1 or answers or authority:
        return None
    cursor = 12
    labels = []
    while True:
        if cursor >= len(packet):
            return None
        length = packet[cursor]
        cursor += 1
        if length == 0:
            break
        if length > 63 or cursor + length > len(packet):
            return None
        try:
            labels.append(packet[cursor:cursor + length].decode("ascii"))
        except UnicodeDecodeError:
            return None
        cursor += length
    if cursor + 4 > len(packet):
        return None
    qtype, qclass = struct.unpack("!HH", packet[cursor:cursor + 4])
    if ".".join(labels).lower() + "." != DOMAIN or (qtype, qclass) != (1, 1):
        return None
    # Keep the authentic question bytes (including any mixed-case labels).
    question = packet[12:cursor + 4]
    response_flags = 0x8480 | (flags & 0x0100)  # QR, AA, RA; preserve RD
    header = struct.pack("!HHHHHH", identifier, response_flags, 1, 1, 0, 0)
    record = b"\xc0\x0c" + struct.pack("!HHIH", 1, 1, 60, 4)
    return header + question + record + socket.inet_aton(ANSWER)


def main() -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.bind(("127.0.0.1", PORT))
        print(f"[LWIP DNS HOST READY] udp 127.0.0.1:{PORT}", flush=True)
        while True:
            packet, peer = server.recvfrom(4096)
            reply = answer_query(packet)
            if reply is None:
                print("[LWIP DNS HOST IGNORE] malformed/unexpected question", flush=True)
                continue
            print("[LWIP DNS HOST QUERY] exact signed-dig A naseed.test.", flush=True)
            server.sendto(reply, peer)
            print(f"[LWIP DNS HOST REPLY] {ANSWER}", flush=True)


if __name__ == "__main__":
    main()
