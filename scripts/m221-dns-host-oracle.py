#!/usr/bin/env python3
"""Independent HOST authoritative RFC2136 update/query server for M221.

The shared M142 DNS packet parser supplies strict wire decoding. Each native
QEMU boot gets its own fresh authoritative zone state, process, address and
log. The server never receives a preseeded update, and query answers become
available only AFTER it receives a real original nsupdate transaction.
"""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import socket
import sys


BASE = Path(__file__).with_name("m142-dns-update-server.py")
spec = spec_from_file_location("m221_m142_dns", BASE)
if spec is None or spec.loader is None:
    raise RuntimeError("M221 requires the verified M142 RFC2136 DNS decoder")
dns = module_from_spec(spec)
spec.loader.exec_module(dns)

PHASES = {
    "first": ("m221-first.naseed.test.", "192.0.2.221"),
    "reopen": ("m221-reopen.naseed.test.", "192.0.2.222"),
}
PORT = 15416


def main() -> None:
    if len(sys.argv) != 2 or sys.argv[1] not in PHASES:
        raise SystemExit("usage: m221-dns-host-oracle.py first|reopen")
    phase = sys.argv[1]
    owner, address = PHASES[phase]
    dns.OWNER = owner
    dns.ADDRESS = address
    oracle = dns.Oracle()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.bind(("127.0.0.1", PORT))
        print(f"[M221 HOST READY] phase={phase} port={PORT} owner={owner} ip={address}", flush=True)
        while True:
            packet, peer = server.recvfrom(4096)
            response, event = oracle.handle(packet)
            if event == "UPDATE":
                print(f"[M221 HOST UPDATE] phase={phase} owner={owner} ip={address} ttl=60", flush=True)
            elif event == "QUERY":
                print(f"[M221 HOST QUERY] phase={phase} owner={owner} type=A", flush=True)
            elif event == "SOA":
                print(f"[M221 HOST SOA] phase={phase} original asked zone metadata", flush=True)
            else:
                print(f"[M221 HOST REJECT] phase={phase} length={len(packet)}", flush=True)
            if response is not None:
                server.sendto(response, peer)
                if event == "UPDATE":
                    print(f"[M221 HOST ACK] phase={phase} status=RFC2136_NOERROR", flush=True)
                elif event == "QUERY":
                    print(f"[M221 HOST ANSWER] phase={phase} owner={owner} ip={address}", flush=True)


if __name__ == "__main__":
    main()
