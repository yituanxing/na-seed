#!/usr/bin/env python3
"""Verify visible QEMU ramfb pixels independently of the guest fbdev driver.

The guest's serial log is only a capture *trigger*. Success requires QMP to
return a true `screendump` from QEMU's display surface, with exact RGB pixels.
"""

import argparse
import json
from pathlib import Path
import socket
import sys
import time


SAMPLES = ((32, 0, (255, 0, 0)),
           (160, 0, (0, 255, 0)),
           (288, 0, (0, 0, 255)),
           (416, 0, (255, 255, 255)))


def read_json(reader, deadline):
    while time.monotonic() < deadline:
        try:
            line = reader.readline()
        except (socket.timeout, TimeoutError):
            continue
        if not line:
            raise RuntimeError("QMP socket closed before response")
        return json.loads(line.decode("utf-8"))
    raise RuntimeError("QMP timed out")


def qmp_command(sock, reader, name, arguments=None):
    payload = {"execute": name}
    if arguments is not None:
        payload["arguments"] = arguments
    sock.sendall((json.dumps(payload, separators=(",", ":")) + "\n").encode())
    deadline = time.monotonic() + 12
    while True:
        response = read_json(reader, deadline)
        if "event" in response:
            continue
        if "error" in response:
            raise RuntimeError(f"QMP {name} rejected request: {response['error']}")
        if "return" in response:
            return response["return"]
        raise RuntimeError(f"unexpected QMP {name} response: {response}")


def ppm_tokens(data, needed=4):
    """Read P6 header tokens without decoding arbitrary raw binary as text."""
    pos = 0
    tokens = []
    while len(tokens) < needed:
        while pos < len(data) and data[pos] in b" \t\r\n":
            pos += 1
        if pos < len(data) and data[pos] == ord("#"):
            while pos < len(data) and data[pos] != 10:
                pos += 1
            continue
        start = pos
        while pos < len(data) and data[pos] not in b" \t\r\n":
            pos += 1
        if start == pos or pos >= len(data):
            raise RuntimeError("truncated QEMU screendump PPM header")
        tokens.append(data[start:pos])
    if data[pos:pos + 1] not in (b" ", b"\t", b"\r", b"\n"):
        raise RuntimeError("missing PPM header/pixels separator")
    if data[pos:pos + 2] == b"\r\n":
        pos += 2
    else:
        pos += 1
    return tokens, data[pos:]


def verify_ppm(path):
    data = path.read_bytes()
    tokens, pixels = ppm_tokens(data)
    if tokens[0] != b"P6" or tokens[3] != b"255":
        raise RuntimeError(f"not canonical QEMU RGB888 PPM: {tokens}")
    width, height = int(tokens[1]), int(tokens[2])
    if width < 512 or height < 1 or width > 8192 or height > 8192:
        raise RuntimeError(f"implausible physical screen: {width}x{height}")
    if len(pixels) != width * height * 3:
        raise RuntimeError(f"QEMU PPM length mismatch: {len(pixels)} != {width * height * 3}")
    for x, y, expected in SAMPLES:
        start = (y * width + x) * 3
        actual = tuple(pixels[start:start + 3])
        if actual != expected:
            raise RuntimeError(f"visible QEMU pixel ({x},{y}): expected {expected}, got {actual}")
    print(f"[M161 QMP DISPLAY PASS] {width}x{height} exact four RGB bands from actual emulated screen", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socket", required=True, type=Path)
    parser.add_argument("--serial", required=True, type=Path)
    parser.add_argument("--screenshot", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=700)
    args = parser.parse_args()
    deadline = time.monotonic() + args.timeout

    while time.monotonic() < deadline:
        if args.serial.is_file():
            trace = args.serial.read_bytes()
            if b"[FAIL]" in trace:
                raise RuntimeError("guest kernel reported FAIL before framebuffer screenshot")
            if trace.count(b"M161_SCREEN_DRAWN") == 1:
                break
        time.sleep(0.1)
    else:
        raise RuntimeError("guest did not draw the M161 frame within budget")

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(2)
    while time.monotonic() < deadline:
        try:
            sock.connect(str(args.socket))
            break
        except (FileNotFoundError, ConnectionRefusedError):
            time.sleep(0.1)
    else:
        raise RuntimeError("QEMU QMP socket never became available")

    with sock:
        reader = sock.makefile("rb")
        greeting = read_json(reader, time.monotonic() + 10)
        if "QMP" not in greeting:
            raise RuntimeError("QEMU QMP greeting missing")
        qmp_command(sock, reader, "qmp_capabilities")
        qmp_command(sock, reader, "screendump", {"filename": str(args.screenshot.resolve())})
    if not args.screenshot.is_file():
        raise RuntimeError("QEMU screendump failed to write screenshot")
    verify_ppm(args.screenshot)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"[M161 QMP FAIL] {exc}", file=sys.stderr, flush=True)
        raise SystemExit(1)
