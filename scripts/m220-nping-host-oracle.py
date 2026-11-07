#!/usr/bin/env python3
"""Host-only UDP socket oracle for the real original Alpine Nping on QEMU.

This process lives on the QEMU host, never in NaSeed's guest. The first and
second native QEMU boot use distinct fixed-length wire payloads and distinct
oracle processes/logs. A guest banner, CLI exit=0 or network counter does not
make this oracle print a receive event.
"""
import socket
import sys


PAYLOAD = {"first": b"NASEED_NPING_FIRST", "reopen": b"NASEED_NPING_AGAIN"}
PORT = 15416


def main() -> None:
    if len(sys.argv) != 2 or sys.argv[1] not in PAYLOAD:
        raise SystemExit("usage: m220-nping-host-oracle.py first|reopen")
    phase = sys.argv[1]
    expected = PAYLOAD[phase]
    assert len(expected) == 18
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as host:
        host.bind(("127.0.0.1", PORT))
        print(f"[M220 HOST READY] phase={phase} port={PORT}", flush=True)
        while True:
            data, peer = host.recvfrom(4096)
            if data == expected:
                print(f"[M220 HOST UDP RECV] phase={phase} "
                      f"payload={expected.decode('ascii')} "
                      f"peer={peer[0]}:{peer[1]}", flush=True)
            else:
                print(f"[M220 HOST UDP REJECT] phase={phase} size={len(data)}",
                      flush=True)


if __name__ == "__main__":
    main()
