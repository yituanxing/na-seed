#!/usr/bin/env python3
"""Offline strict-packet proof for the independent Alpine dig wire oracle."""

import importlib.util
import pathlib
import socket
import struct
import unittest


path = pathlib.Path(__file__).with_name("lwip-dns-wire-server.py")
spec = importlib.util.spec_from_file_location("naseed_dns_oracle", path)
assert spec and spec.loader
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


def question(name=b"\x06naseed\x04test\x00", kind=1, clazz=1) -> bytes:
    return struct.pack("!HHHHHH", 0xCAFE, 0x0100, 1, 0, 0, 0) + name + struct.pack(
        "!HH", kind, clazz
    )


class PacketOracleTests(unittest.TestCase):
    def test_valid_a_packet_keeps_question_and_exact_answer(self):
        request = question()
        response = oracle.answer_query(request)
        self.assertIsNotNone(response)
        self.assertEqual(response[:2], b"\xca\xfe")
        self.assertEqual(struct.unpack("!HHHHHH", response[:12])[2:], (1, 1, 0, 0))
        self.assertEqual(response[12:12 + len(request) - 12], request[12:])
        self.assertEqual(response[-4:], socket.inet_aton("192.0.2.42"))
        self.assertEqual(response[-16:-14], b"\xc0\x0c")

    def test_nonmatching_and_malformed_packets_are_ignored(self):
        for request in (
            b"",
            question()[:13],
            question(name=b"\x05other\x04test\x00"),
            question(kind=28),
            question(clazz=3),
            question(name=b"\xc0\x0c"),
            struct.pack("!HHHHHH", 1, 0x8100, 1, 0, 0, 0)
            + question()[12:],
        ):
            with self.subTest(packet=request):
                self.assertIsNone(oracle.answer_query(request))


if __name__ == "__main__":
    unittest.main()
