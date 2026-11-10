#!/usr/bin/env python3
"""Independent H12 real QEMU USB HID/evtest hardware-input oracle.

Only sends KEY_A through QEMU's QMP `input-send-event` endpoint. PASS demands
the original signed Alpine evtest process's raw EV_KEY press/release records,
each followed by SYN_REPORT, plus the guest's real evtest completion. No
serial-injected key, synthetic evdev event or boot-only marker can pass.
"""

import argparse
from pathlib import Path
import re
import runpy
import socket
import sys
import time


qmp = runpy.run_path('scripts/m161-qmp-verify.py',
                     run_name='h12_qmp_library')

READY = b'H12_HID_READY_FOR_QMP'
DOWN = b'H12_HID_A_DOWN_WITH_SYN'
UP = b'H12_HID_A_UP_WITH_SYN'
GUEST_PASS = b'H12_ORIGINAL_ALPINE_EVTEST_USB_PASS'
FATALS = (
    b'[H12 XHCI FAIL', b'[H12 USB HID FAIL', b'[H12 HID FAIL',
    b'[H12 USB ROOT FAIL', b'[FAIL]', b'[FAULT]',
    b'[EXEC REJECT]', b'[ELF REJECT]',
)
KEY_PATTERN = re.compile(
    r'\btype\s+1\s+\(EV_KEY\),\s*code\s+30\s+\(KEY_A\),\s*value\s+([01])\b'
)


def snapshot(serial: Path) -> bytes:
    return serial.read_bytes() if serial.is_file() else b''


def no_fatal(raw: bytes):
    for marker in FATALS:
        if marker in raw:
            raise RuntimeError(f'guest reported fatal serial marker {marker!r}')


def wait_for(serial: Path, token: bytes, deadline: float) -> bytes:
    while time.monotonic() < deadline:
        raw = snapshot(serial)
        no_fatal(raw)
        if raw.count(token) > 1:
            raise RuntimeError(f'guest duplicated {token!r}')
        if token in raw:
            return raw
        time.sleep(0.1)
    raise RuntimeError(f'timed out waiting for real guest {token!r}')


def connect_qmp(path: Path, deadline: float):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(2)
    while time.monotonic() < deadline:
        try:
            sock.connect(str(path))
            break
        except (ConnectionRefusedError, FileNotFoundError):
            time.sleep(0.1)
    else:
        sock.close()
        raise RuntimeError(f'QEMU QMP socket unavailable: {path}')
    reader = sock.makefile('rb')
    greeting = qmp['read_json'](reader, time.monotonic() + 10)
    if 'QMP' not in greeting:
        raise RuntimeError(f'missing true QMP greeting: {greeting}')
    qmp['qmp_command'](sock, reader, 'qmp_capabilities')
    return sock, reader


def key_event(down: bool):
    return {
        'type': 'key',
        'data': {'down': down,
                 'key': {'type': 'qcode', 'data': 'a'}},
    }


def parse_hardware_evtest(raw: bytes):
    text = raw.decode('utf-8', errors='replace').replace('\r', '')
    lines = text.splitlines()
    # Locate only original evtest verbose output lines, never pass markers
    # printed by scripts, CPO blobs, or kernel debug instrumentation.
    records = []
    for index, line in enumerate(lines):
        match = KEY_PATTERN.search(line)
        if match:
            records.append((index, int(match.group(1))))
    return lines, records


def seen_key_syn(serial: Path, value: int, previous_index: int,
                 deadline: float) -> int:
    while time.monotonic() < deadline:
        raw = snapshot(serial)
        no_fatal(raw)
        lines, records = parse_hardware_evtest(raw)
        for key_index, observed_value in records:
            if key_index <= previous_index or observed_value != value:
                continue
            # A SYN event must follow this actual original evtest KEY_A record
            # before any later KEY_A transition; otherwise the USB/evdev
            # packet boundary has not been observed.
            next_keys = [i for i, _ in records if i > key_index]
            upper = min(next_keys) if next_keys else len(lines)
            if any('SYN_REPORT' in line for line in lines[key_index + 1:upper]):
                return key_index
        time.sleep(0.1)
    raise RuntimeError(f'original signed evtest missing KEY_A value={value} + SYN_REPORT')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--serial', required=True, type=Path)
    parser.add_argument('--qmp', required=True, type=Path)
    parser.add_argument('--timeout', type=int, default=300)
    args = parser.parse_args()
    deadline = time.monotonic() + args.timeout

    wait_for(args.serial, READY, deadline)
    sock, reader = connect_qmp(args.qmp, deadline)
    with sock:
        # A PASS is only valid for input injected by *this* live QMP session.
        # Neither historical serial records nor premature input from some
        # other controller may satisfy the hardware test before our first
        # deliberate QMP key-down command.
        before = snapshot(args.serial)
        no_fatal(before)
        before_lines, before_records = parse_hardware_evtest(before)
        if before_records or any(marker in before for marker in
                                 (DOWN, UP, GUEST_PASS)):
            raise RuntimeError(
                'evtest key event/success predated the QMP hardware injection')
        baseline_index = len(before_lines) - 1
        qmp['qmp_command'](sock, reader, 'input-send-event',
                           {'events': [key_event(True)]})
        down_index = seen_key_syn(args.serial, 1, baseline_index, deadline)
        # The original evtest --query must independently observe A held
        # BEFORE release, not race against an immediately injected key-up.
        wait_for(args.serial, DOWN, deadline)
        qmp['qmp_command'](sock, reader, 'input-send-event',
                           {'events': [key_event(False)]})
        seen_key_syn(args.serial, 0, down_index, deadline)
        wait_for(args.serial, UP, deadline)
        wait_for(args.serial, GUEST_PASS, deadline)

    print('[H12 QMP REAL USB HID EVTEST PASS] explicit qemu-xhci+usb-kbd physical Key A down/up -> original signed Alpine evtest EV_KEY code 30 value 1 SYN and value 0 SYN', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError) as error:
        print(f'[H12 QMP REAL USB HID FAIL] {error}', file=sys.stderr,
              flush=True)
        raise SystemExit(1)
