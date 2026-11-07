#!/usr/bin/env python3
"""Independent host DNS+SMTP wire oracle for original guest Alpine programs.

DNS: a real authoritative UDP A answer, only for m152.example.test.
SMTP: a real RFC 5321 command/data exchange with a checked message body.
Neither a guest echo nor an unconfirmed TCP connect can produce a host PASS.
"""
import ipaddress
import socketserver
import struct
import threading


QNAME = "m152.example.test"
ANSWER = "198.51.100.152"
SUBJECT = b"Subject: M152-SMTP-WIRE"
BODY = b"M152 real original Alpine SMTP client payload"


class DNSHandler(socketserver.BaseRequestHandler):
    def handle(self):
        packet, wire = self.request
        if len(packet) < 17:
            return
        xid, flags, questions, _, _, _ = struct.unpack("!HHHHHH", packet[:12])
        if flags & 0x8000 or questions != 1:
            return
        offset = 12
        labels = []
        while offset < len(packet):
            size = packet[offset]
            offset += 1
            if size == 0:
                break
            # A compressed question, a malformed label, or short packet is
            # not a valid independent test of our queried absolute domain.
            if size > 63 or offset + size > len(packet):
                return
            labels.append(packet[offset:offset + size].decode("ascii", "replace"))
            offset += size
        if offset + 4 > len(packet):
            return
        qtype, qclass = struct.unpack("!HH", packet[offset:offset + 4])
        name = ".".join(labels).lower()
        if (name, qtype, qclass) != (QNAME, 1, 1):
            print(f"[M152 HOST DNS OTHER] {name} type={qtype} class={qclass}", flush=True)
            return
        question = packet[12:offset + 4]
        header = struct.pack("!HHHHHH", xid, 0x8180, 1, 1, 0, 0)
        answer = (
            b"\xc0\x0c" + struct.pack("!HHIH", 1, 1, 60, 4) +
            ipaddress.IPv4Address(ANSWER).packed
        )
        wire.sendto(header + question + answer, self.client_address)
        print(f"[M152 HOST DNS ANSWER] {name} A {ANSWER}", flush=True)


class DNSOracle(socketserver.ThreadingUDPServer):
    allow_reuse_address = True
    daemon_threads = True


class SMTPHandler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(20)
        incoming = self.request.makefile("rb")

        def reply(line):
            self.request.sendall(line.encode("ascii") + b"\r\n")

        reply("220 m152.example.test ESMTP oracle")
        sender = recipient = ""
        greeted = False
        in_data = False
        content = bytearray()
        for _ in range(256):
            try:
                wire = incoming.readline(16384)
            except TimeoutError:
                break
            if not wire:
                break
            line = wire.rstrip(b"\r\n")
            if in_data:
                if line == b".":
                    good = (
                        greeted and sender.startswith("MAIL FROM:") and
                        recipient.startswith("RCPT TO:") and
                        SUBJECT in content and BODY in content
                    )
                    if good:
                        print(
                            f"[M152 HOST SMTP DELIVERED] subject=M152-SMTP-WIRE "
                            f"bytes={len(content)}",
                            flush=True,
                        )
                        reply("250 2.0.0 message queued")
                    else:
                        print("[M152 HOST SMTP REJECT] missing verified message or envelope", flush=True)
                        reply("554 5.6.0 verification failed")
                    in_data = False
                    content.clear()
                else:
                    if len(content) + len(wire) > 65536:
                        reply("552 5.3.4 message too large")
                        break
                    content.extend(wire[1:] if wire.startswith(b"..") else wire)
                continue
            command = line.decode("utf-8", "replace")
            upper = command.upper()
            if upper.startswith("EHLO ") or upper.startswith("HELO "):
                greeted = True
                reply("250-m152.example.test")
                reply("250 SIZE 65536")
            elif upper.startswith("MAIL FROM:"):
                sender = upper
                reply("250 2.1.0 sender ok")
            elif upper.startswith("RCPT TO:"):
                recipient = upper
                reply("250 2.1.5 recipient ok")
            elif upper == "DATA":
                if not (sender and recipient):
                    reply("503 5.5.1 envelope required")
                else:
                    reply("354 end with <CRLF>.<CRLF>")
                    in_data = True
            elif upper == "QUIT":
                reply("221 2.0.0 bye")
                break
            elif upper == "RSET":
                sender = recipient = ""
                reply("250 2.0.0 reset")
            else:
                reply("500 5.5.2 unsupported command")


class SMTPOracle(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    smtp = SMTPOracle(("127.0.0.1", 15425), SMTPHandler)
    dns = DNSOracle(("127.0.0.1", 15453), DNSHandler)
    threading.Thread(target=smtp.serve_forever, daemon=True).start()
    print("[M152 HOST PROTOCOLS READY]", flush=True)
    try:
        dns.serve_forever()
    finally:
        dns.server_close()
        smtp.shutdown()
        smtp.server_close()
