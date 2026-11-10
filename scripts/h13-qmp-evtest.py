#!/usr/bin/env python3
"""Independent H13 genuine QEMU USB Boot Mouse/original Alpine evtest oracle.

QMP injects physical relative X/Y motion and BTN_LEFT down/up. An original
signed evtest BUS_USB device identity and real EV_REL/EV_KEY + SYN_REPORT
records are mandatory; command ACK or guest markers alone cannot PASS.
"""

import argparse
from pathlib import Path
import re
import runpy
import socket
import sys
import time


qmp = runpy.run_path('scripts/m161-qmp-verify.py',
                     run_name='h13_qmp_library')

READY = b'H13_MOUSE_READY_FOR_QMP'
STAGES = ((b'H13_MOUSE_REL_X_WITH_SYN', 'x'),
          (b'H13_MOUSE_REL_Y_WITH_SYN', 'y'),
          (b'H13_MOUSE_BTN_DOWN_WITH_SYN', 'down'),
          (b'H13_MOUSE_BTN_UP_WITH_SYN', 'up'))
GUEST_PASS = b'H13_ORIGINAL_ALPINE_MOUSE_EVTEST_USB_PASS'
FATALS = (
    b'[H13 MOUSE FAIL', b'[H13 XHCI FAIL',
    b'[FAIL]', b'[FAULT]', b'[EXEC REJECT]', b'[ELF REJECT]',
    b'H13_MOUSE_EVTEST_FAIL_STAGE_',
)
EVENT_PATTERN = re.compile(
    r'^Event: time .*type\s+([12])\s+\((EV_KEY|EV_REL)\),\s*'
    r'code\s+(\d+)\s+\((BTN_LEFT|REL_X|REL_Y)\),\s*value\s+(-?\d+)\b'
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


def mouse_event(stage: str):
    if stage == 'x':
        return {'type': 'rel', 'data': {'axis': 'x', 'value': 20}}
    if stage == 'y':
        return {'type': 'rel', 'data': {'axis': 'y', 'value': -12}}
    if stage in ('down', 'up'):
        return {'type': 'btn',
                'data': {'button': 'left', 'down': stage == 'down'}}
    raise ValueError(stage)



def parse_hardware_evtest(raw: bytes):
    lines = raw.decode('utf-8', errors='replace').replace('\r', '').splitlines()
    records = []
    for index, line in enumerate(lines):
        match = EVENT_PATTERN.search(line)
        if match:
            records.append((index, int(match.group(1)),
                            int(match.group(3)), int(match.group(5))))
    return lines, records


def seen_event_syn(serial: Path, stage: str, previous_index: int,
                   deadline: float) -> int:
    expected_type, expected_code, expected_value = {
        'x': (2, 0, None),
        'y': (2, 1, None),
        'down': (1, 272, 1),
        'up': (1, 272, 0),
    }[stage]
    while time.monotonic() < deadline:
        raw = snapshot(serial)
        no_fatal(raw)
        lines, entries = parse_hardware_evtest(raw)
        for index, ev_type, ev_code, ev_value in entries:
            if index <= previous_index or (ev_type, ev_code) != (
                    expected_type, expected_code):
                continue
            if expected_value is None and ev_value == 0:
                continue
            if expected_value is not None and ev_value != expected_value:
                continue
            if any(line.startswith('Event: time ') and
                   'SYN_REPORT' in line for line in lines[index+1:index+8]):
                return index
        time.sleep(0.1)
    raise RuntimeError(f'original signed Alpine evtest missing {stage} and SYN_REPORT')



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
        before = snapshot(args.serial)
        no_fatal(before)
        before_lines, before_records = parse_hardware_evtest(before)
        if before_records or GUEST_PASS in before or any(
                token in before for token, _ in STAGES):
            raise RuntimeError('original evtest event or PASS predated QMP injection')
        if (b'Input device ID: bus 0x3' not in before or
                b'Input device name: "QEMU USB HID Boot Mouse"' not in before):
            raise RuntimeError('original evtest did not verify BUS_USB mouse')

        mice = qmp['qmp_command'](sock, reader, 'query-mice')
        if not isinstance(mice, list):
            raise RuntimeError(f'query-mice unexpected return {mice!r}')
        # QEMU's `usb-mouse` registers the QMP pointer as "QEMU HID Mouse",
        # while the guest evtest identifies the actual device as BUS_USB.
        # Require that this (not the separately listed PS/2 mouse) is the
        # currently selected relative pointer before injecting any events.
        usb_mice = [m for m in mice if isinstance(m, dict) and
                    m.get('name') == 'QEMU HID Mouse' and
                    m.get('current') is True and
                    m.get('absolute') is False]
        if len(usb_mice) != 1:
            raise RuntimeError(f'QMP did not enumerate unique relative USB mouse {mice!r}')

        # Stage each QMP command only after the previous original evtest
        # event was independently observed; PS/2 cannot pass BUS_USB test.
        baseline = len(before_lines) - 1
        for token, stage in STAGES:
            qmp['qmp_command'](sock, reader, 'input-send-event',
                               {'events': [mouse_event(stage)]})
            seen_event_syn(args.serial, stage, baseline, deadline)
            wait_for(args.serial, token, deadline)
            baseline = len(snapshot(args.serial).replace(b'\r', b'').splitlines()) - 1
        wait_for(args.serial, GUEST_PASS, deadline)

    print('[H13 QMP REAL USB MOUSE EVTEST PASS] genuine QEMU USB Mouse REL_X/Y + BTN_LEFT 1/0 each with SYN_REPORT via original signed Alpine BUS_USB evtest', flush=True)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError) as error:
        print(f'[H13 QMP REAL USB MOUSE FAIL] {error}', file=sys.stderr,
              flush=True)
        raise SystemExit(1)
