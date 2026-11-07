#!/usr/bin/env python3
"""Offline packet-level proof for the independent RFC 2136 host oracle."""

import importlib.util
import pathlib
import socket
import struct
import unittest


spec = importlib.util.spec_from_file_location(
    "naseed_m142_oracle", pathlib.Path(__file__).with_name("m142-dns-update-server.py")
)
assert spec and spec.loader
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


def name(text: str) -> bytes:
    return b"".join(bytes((len(part),)) + part.encode("ascii")
                    for part in text.rstrip(".").split(".")) + b"\0"


def update(owner="updated.naseed.test.", address="192.0.2.99") -> bytes:
    question = name("naseed.test.") + struct.pack("!HH", 6, 1)
    record = name(owner) + struct.pack("!HHIH", 1, 1, 60, 4)
    record += socket.inet_aton(address)
    return struct.pack("!HHHHHH", 0x1234, 0x2800, 1, 0, 1, 0) + question + record


def lookup(owner="updated.naseed.test.", rtype=1) -> bytes:
    return struct.pack("!HHHHHH", 0xFEED, 0x0100, 1, 0, 0, 0) + name(owner) + struct.pack(
        "!HH", rtype, 1
    )


class Rfc2136OracleTests(unittest.TestCase):
    def test_update_then_query_has_real_atomic_state(self):
        server = oracle.Oracle()
        self.assertEqual(server.handle(lookup()), (None, None))
        response, event = server.handle(update())
        self.assertEqual(event, "UPDATE")
        self.assertEqual(response[:2], b"\x12\x34")
        self.assertEqual(struct.unpack("!H", response[2:4])[0], 0xA800)
        self.assertEqual(server.updates, 1)
        self.assertTrue(server.updated)
        answer, event = server.handle(lookup())
        self.assertEqual(event, "QUERY")
        self.assertEqual(answer[:2], b"\xfe\xed")
        self.assertEqual(answer[-4:], socket.inet_aton("192.0.2.99"))
        self.assertEqual(server.queries, 1)

    def test_invalid_zone_owner_or_rdata_never_mutates_state(self):
        for packet in (
            b"",
            update(owner="other.naseed.test."),
            update(address="192.0.2.100"),
            update()[:20],
            update().replace(name("naseed.test."), name("wrong.test."), 1),
        ):
            with self.subTest(packet=packet):
                server = oracle.Oracle()
                self.assertEqual(server.handle(packet), (None, None))
                self.assertFalse(server.updated)

    def test_known_soa_and_exact_nping_payload_only(self):
        server = oracle.Oracle()
        reply, event = server.handle(lookup("naseed.test.", 6))
        self.assertEqual(event, "SOA")
        self.assertIn(b"hostmaster", reply)
        self.assertEqual(server.handle(oracle.NPING_PAYLOAD), (None, "NPING"))
        self.assertEqual(server.nping, 1)
        self.assertEqual(server.handle(b"NASEED_NPING_PROB"), (None, None))


if __name__ == "__main__":
    unittest.main()
