#!/usr/bin/env python3
"""Independent H15 real FT232BM USB serial chardev oracle.

This oracle connects to the QEMU usb-serial chardev BEFORE guest USB enumeration
(QEMU FTDI attaches on backend CHR_EVENT_OPENED), waits until original signed
Alpine BusyBox stty opens the real /dev/ttyUSB0, sends a cryptographically
random 32-byte printable challenge, and requires its EXACT physical USB return.
A guest-only log marker, static fixture echo or QEMU green exit cannot pass.
"""
import argparse
import hashlib
from pathlib import Path
import secrets
import socket
import sys
import time


FATAL = (b'[H15 FTDI FAIL', b'H15_FTDI_GUEST_FAIL_',
         b'[FAIL]', b'[FAULT]', b'[EXEC REJECT]', b'[ELF REJECT]')
READY = b'H15_USB_SERIAL_READY_FOR_HOST'
GUEST_REPLY = b'H15_ORIGINAL_ALPINE_BUSYBOX_USB_SERIAL_NONCE_RETURNED'
KERNEL_ATTACHED = b'[H15 USB SERIAL ATTACHED] /dev/ttyUSB0 FT232BM BUS_USB'


def snapshot(path):
    return path.read_bytes().replace(b'\r', b'') if path.is_file() else b''


def assert_no_fatal(raw):
    for marker in FATAL:
        if marker in raw:
            raise RuntimeError(f'guest fatal serial token: {marker!r}')


def wait_serial(serial, marker, until):
    while time.monotonic() < until:
        raw = snapshot(serial)
        assert_no_fatal(raw)
        if raw.count(marker) > 1:
            raise RuntimeError(f'duplicate guest serial marker {marker!r}')
        if marker in raw:
            return raw
        time.sleep(0.08)
    raise RuntimeError(f'original guest marker timed out {marker!r}')


def connect_early(socket_path, until):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(0.4)
    while time.monotonic() < until:
        try:
            sock.connect(str(socket_path))
            sock.settimeout(0.4)
            return sock
        except (FileNotFoundError, ConnectionRefusedError):
            time.sleep(0.04)
    sock.close()
    raise RuntimeError('actual QEMU FT232BM Unix chardev never accepted socket')


def read_exact(sock, count, until):
    out = bytearray()
    while len(out) < count and time.monotonic() < until:
        try:
            chunk = sock.recv(count - len(out))
        except socket.timeout:
            continue
        if not chunk:
            raise RuntimeError(
                f'FTDI chardev ended after {len(out)}/{count} bytes')
        out.extend(chunk)
    if len(out) != count:
        raise RuntimeError(f'incomplete serial return {len(out)}/{count}')
    return bytes(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--socket', type=Path, required=True)
    parser.add_argument('--serial', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=300)
    args = parser.parse_args()
    until = time.monotonic() + args.timeout
    sock = connect_early(args.socket, until)
    try:
        # Socket connection is intentionally EARLY: QEMU's default usb-serial
        # does not attach to USB until the chardev backend becomes opened.
        before = wait_serial(args.serial, READY, until)
        if KERNEL_ATTACHED not in before:
            raise RuntimeError('missing actual FT232BM xHCI/ttyUSB0 descriptor evidence')
        if b'[EXEC] replaced process with lwext4 ELF: /bin/busybox' not in before:
            raise RuntimeError('original Alpine BusyBox was not executed from NVMe ext4')
        if GUEST_REPLY in before:
            raise RuntimeError('guest asserted nonce return before socket challenge')
        # No emulated backend may have already echoed data before host injection.
        sock.settimeout(0.05)
        try:
            unexpected = sock.recv(1)
            if unexpected:
                raise RuntimeError(f'unsolicited pre-challenge USB serial data {unexpected!r}')
        except socket.timeout:
            pass
        challenge = secrets.token_hex(16).encode('ascii')
        if len(challenge) != 32:
            raise RuntimeError('host challenge length invalid')
        sock.settimeout(0.6)
        sock.sendall(challenge)
        received = read_exact(sock, len(challenge), until)
        if received != challenge:
            raise RuntimeError(
                'original BusyBox did not return exact randomly generated USB-serial bytes')
        wait_serial(args.serial, GUEST_REPLY, until)
        # A further unsolicited post-response byte is not valid. Original
        # BusyBox sends exactly challenge bytes to the chardev, no framing.
        sock.settimeout(0.08)
        try:
            extra = sock.recv(1)
            if extra:
                raise RuntimeError(f'extra FTDI serial data after nonce {extra!r}')
        except socket.timeout:
            pass
        print('[H15 FTDI HOST RANDOM NONCE ROUNDTRIP PASS] '
              'one real QEMU usb-serial 0403:6001 -> original Alpine '
              'BusyBox /dev/ttyUSB0 raw read/write -> exact host socket return; '
              f'nonce_sha256={hashlib.sha256(challenge).hexdigest()}',
              flush=True)
    finally:
        sock.close()


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print(f'[H15 FTDI HOST RANDOM NONCE FAIL] {exc}', file=sys.stderr, flush=True)
        sys.exit(1)
