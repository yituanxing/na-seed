#!/usr/bin/env python3
"""Deterministic host-only UDP echo oracle for NaSeed's real NIC smoke.

Runs on the GitHub runner loopback; QEMU slirp guests reach it at 10.0.2.2.
Only a byte-exact original Alpine Ncat request elicits the expected reply.
"""
import socket


with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
    server.bind(("127.0.0.1", 15414))
    print("[LWIP UDP HOST READY] 127.0.0.1:15414", flush=True)
    while True:
        payload, peer = server.recvfrom(2048)
        if payload != b"M141_UDP_REQUEST\n":
            print("[LWIP UDP HOST REJECT] unexpected datagram", flush=True)
            continue
        print("[LWIP UDP HOST REQUEST] exact signed-Ncat payload", flush=True)
        server.sendto(b"M141_UDP_REPLY\n", peer)
