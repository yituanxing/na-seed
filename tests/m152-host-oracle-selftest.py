#!/usr/bin/env python3
"""Host-only DNS/SMTP oracle contract test (NOT a guest protocol PASS)."""
import ipaddress
from pathlib import Path
import select
import socket
import struct
import subprocess
import sys
import time


def assert_dns():
    name = "m152.example.test"
    question = b"".join(bytes((len(label),)) + label.encode()
                        for label in name.split(".")) + b"\0\0\1\0\1"
    request = struct.pack("!HHHHHH", 0x152, 0x0100, 1, 0, 0, 0) + question
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(4)
        sock.sendto(request, ("127.0.0.1", 15453))
        answer, _ = sock.recvfrom(4096)
    xid, flags, qd, an, ns, ar = struct.unpack("!HHHHHH", answer[:12])
    assert (xid, flags, qd, an, ns, ar) == (0x152, 0x8180, 1, 1, 0, 0)
    assert answer[12:12 + len(question)] == question
    assert answer.endswith(ipaddress.IPv4Address("198.51.100.152").packed)


def assert_smtp():
    with socket.create_connection(("127.0.0.1", 15425), timeout=5) as conn:
        conn.settimeout(5)
        reader = conn.makefile("rb")

        def command(line, expected):
            if line is not None:
                conn.sendall(line.encode() + b"\r\n")
            reply = reader.readline()
            assert reply.startswith(expected), (line, reply)

        command(None, b"220 ")
        command("EHLO guest.m152.example.test", b"250-")
        command(None, b"250 ")
        command("MAIL FROM:<m152-sender@example.test>", b"250 ")
        command("RCPT TO:<m152-recipient@example.test>", b"250 ")
        command("DATA", b"354 ")
        conn.sendall(
            b"From: m152-sender@example.test\r\n"
            b"To: m152-recipient@example.test\r\n"
            b"Subject: M152-SMTP-WIRE\r\n\r\n"
            b"M152 real original Alpine SMTP client payload\r\n"
            b".\r\n"
        )
        command(None, b"250 ")
        command("QUIT", b"221 ")


def main():
    root = Path(__file__).resolve().parents[1]
    child = subprocess.Popen(
        [sys.executable, "-u", str(root / "scripts/m152-host-oracle.py")],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    output = []
    try:
        ready = False
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if child.poll() is not None:
                break
            readable, _, _ = select.select([child.stdout], [], [], .2)
            if readable:
                line = child.stdout.readline()
                output.append(line)
                if "[M152 HOST PROTOCOLS READY]" in line:
                    ready = True
                    break
        assert ready, "host oracle did not bind both service endpoints"
        assert_dns()
        assert_smtp()
        time.sleep(.2)
    finally:
        child.terminate()
        try:
            rest, _ = child.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            child.kill()
            rest, _ = child.communicate()
        output.append(rest)
    logs = "".join(output)
    assert "[M152 HOST DNS ANSWER] m152.example.test A 198.51.100.152" in logs, logs
    assert "[M152 HOST SMTP DELIVERED] subject=M152-SMTP-WIRE" in logs, logs
    print("[M152 HOST ORACLE SELFTEST PASS] valid DNS reply and SMTP verified DATA")


if __name__ == "__main__":
    main()
