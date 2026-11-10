#!/usr/bin/env python3
"""Offline negative controls for the public-network PCAP oracle."""

import importlib.util
import ipaddress
from pathlib import Path
import struct


source = Path(__file__).with_name("m196-public-wire-check.py")
spec = importlib.util.spec_from_file_location("naseed_m196_wire", source)
assert spec and spec.loader
wire = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wire)


def ip_packet(src, dst, protocol, payload):
    header = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(payload),
                         1, 0, 64, protocol, 0,
                         ipaddress.IPv4Address(src).packed,
                         ipaddress.IPv4Address(dst).packed)
    return b"\x00" * 12 + b"\x08\x00" + header + payload


def udp(src, dst, sport, dport, data):
    return ip_packet(src, dst, 17,
                     struct.pack("!HHHH", sport, dport, 8 + len(data), 0) + data)


def tcp(src, dst, sport, dport, flags, data=b""):
    return ip_packet(src, dst, 6,
                     struct.pack("!HHIIHHHH", sport, dport, 1, 0,
                                 (5 << 12) | flags, 65535, 0, 0) + data)


question = b"\x06dl-cdn\x0balpinelinux\x03org\x00\x00\x01\x00\x01"
ident = 0x1234
query = struct.pack("!HHHHHH", ident, 0x0100, 1, 0, 0, 0) + question
reply = (struct.pack("!HHHHHH", ident, 0x8180, 1, 1, 0, 0) + question +
         b"\xc0\x0c" + struct.pack("!HHIH", 1, 1, 60, 4) +
         ipaddress.IPv4Address("146.75.77.132").packed)
client_hello = b"\x16\x03\x01\x00\x06\x01\x00\x00\x02\x03\x03"
dns_query = udp("10.0.2.15", "10.0.2.3", 53001, 53, query)
dns_reply = udp("10.0.2.3", "10.0.2.15", 53, 53001, reply)
syn = tcp("10.0.2.15", "146.75.77.132", 53002, 443, 0x02)
synack = tcp("146.75.77.132", "10.0.2.15", 443, 53002, 0x12)
hello = tcp("10.0.2.15", "146.75.77.132", 53002, 443, 0x18, client_hello)


def pcap(frames):
    header = b"\xd4\xc3\xb2\xa1" + struct.pack("<HHIIII", 2, 4, 0, 0, 65535, 1)
    return header + b"".join(struct.pack("<IIII", 1, 0, len(frame), len(frame)) + frame
                             for frame in frames)


def rejects(frames):
    try:
        wire.verify_capture(pcap(frames))
    except ValueError:
        return
    raise AssertionError("public wire verifier accepted incomplete evidence")


all_frames = [dns_query, dns_reply, syn, synack, hello]
assert wire.verify_capture(pcap(all_frames))["public_tls_connections"] == 1
for missing in range(len(all_frames)):
    rejects([frame for index, frame in enumerate(all_frames) if index != missing])
rejects([dns_query, dns_reply,
         tcp("10.0.2.15", "10.0.2.2", 53002, 443, 0x02),
         tcp("10.0.2.2", "10.0.2.15", 443, 53002, 0x12),
         tcp("10.0.2.15", "10.0.2.2", 53002, 443, 0x18, client_hello)])
rejects([dns_query, dns_reply, syn, synack,
         tcp("10.0.2.15", "146.75.77.132", 53002, 443, 0x18, b"plaintext")])
rejects([dns_query, dns_reply,
         tcp("10.0.2.15", "8.8.8.8", 53002, 443, 0x02),
         tcp("8.8.8.8", "10.0.2.15", 443, 53002, 0x12),
         tcp("10.0.2.15", "8.8.8.8", 53002, 443, 0x18, client_hello)])
print("[M196 PUBLIC WIRE CONTRACT PASS] matching DNS+public TLS accepted; "
      "missing packet/local or unrelated endpoint/plaintext rejected")
