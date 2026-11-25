#!/usr/bin/env python3
"""Independent QEMU xHCI/evtest oracles for H12 keyboard, H13 mouse and H14 dual HID.

Each profile still demands actual original evtest event+SYN evidence and
rejects prior events, fatal markers and a non-USB mouse. This is not a
serial-input emulator or a substitute for the physical QEMU device tests.
"""
import argparse
from pathlib import Path
import re
import runpy
import socket
import sys
import time

qmp = runpy.run_path('scripts/m161-qmp-verify.py', run_name='hid_qmp_library')

H12_READY = b'H12_HID_READY_FOR_QMP'
H12_DOWN = b'H12_HID_A_DOWN_WITH_SYN'
H12_UP = b'H12_HID_A_UP_WITH_SYN'
H12_GUEST_PASS = b'H12_ORIGINAL_ALPINE_EVTEST_USB_PASS'
H12_FATALS = (
    b'[H12 XHCI FAIL', b'[H12 USB HID FAIL', b'[H12 HID FAIL',
    b'[H12 USB ROOT FAIL', b'[FAIL]', b'[FAULT]',
    b'[EXEC REJECT]', b'[ELF REJECT]',
)
H12_KEY_PATTERN = re.compile(
    r'\btype\s+1\s+\(EV_KEY\),\s*code\s+30\s+\(KEY_A\),\s*value\s+([01])\b'
)

H13_READY = b'H13_MOUSE_READY_FOR_QMP'
H13_STAGES = ((b'H13_MOUSE_REL_X_WITH_SYN', 'x'),
          (b'H13_MOUSE_REL_Y_WITH_SYN', 'y'),
          (b'H13_MOUSE_BTN_DOWN_WITH_SYN', 'down'),
          (b'H13_MOUSE_BTN_UP_WITH_SYN', 'up'))
H13_GUEST_PASS = b'H13_ORIGINAL_ALPINE_MOUSE_EVTEST_USB_PASS'
H13_FATALS = (
    b'[H13 MOUSE FAIL', b'[H13 XHCI FAIL',
    b'[FAIL]', b'[FAULT]', b'[EXEC REJECT]', b'[ELF REJECT]',
    b'H13_MOUSE_EVTEST_FAIL_STAGE_',
)
H13_EVENT_PATTERN = re.compile(
    r'^Event: time .*type\s+([12])\s+\((EV_KEY|EV_REL)\),\s*'
    r'code\s+(\d+)\s+\((BTN_LEFT|REL_X|REL_Y)\),\s*value\s+(-?\d+)\b'
)

H14_KBD_READY = b'H14_KBD_READY_FOR_QMP'
H14_MOUSE_READY = b'H14_MOUSE_READY_FOR_QMP'
H14_GUEST_PASS = b'H14_ORIGINAL_ALPINE_DUAL_EVTEST_USB_PASS'
H14_FATALS = (b'[H14 DUAL FAIL', b'[H14 XHCI FAIL', b'[FAIL]', b'[FAULT]',
          b'[EXEC REJECT]', b'[ELF REJECT]', b'H14_DUAL_EVTEST_FAIL_STAGE_')
H14_KEY_PATTERN = re.compile(r'^Event: time .*type 1 \(EV_KEY\), code 30 \(KEY_A\), value ([01])\b')
H14_MOUSE_PATTERN = re.compile(
    r'^Event: time .*type ([12]) \((EV_KEY|EV_REL)\), '
    r'code (\d+) \((BTN_LEFT|REL_X|REL_Y)\), value (-?\d+)\b'
)

FATALS = {'h12': H12_FATALS, 'h13': H13_FATALS, 'h14': H14_FATALS}


def snapshot(serial: Path) -> bytes:
    return serial.read_bytes() if serial.is_file() else b''


def no_fatal(raw: bytes, profile: str):
    for marker in FATALS[profile]:
        if marker in raw:
            prefix = 'guest fatal marker' if profile == 'h14' else 'guest reported fatal serial marker'
            raise RuntimeError(f'{prefix} {marker!r}')


def wait_for(serial: Path, token: bytes, deadline: float, profile: str) -> bytes:
    while time.monotonic() < deadline:
        raw = snapshot(serial)
        no_fatal(raw, profile)
        if raw.count(token) > 1:
            prefix = 'duplicate guest marker' if profile == 'h14' else 'guest duplicated'
            raise RuntimeError(f'{prefix} {token!r}')
        if token in raw:
            return raw
        time.sleep(0.1)
    prefix = 'guest timeout' if profile == 'h14' else 'timed out waiting for real guest'
    raise RuntimeError(f'{prefix} {token!r}')


def connect_qmp(path: Path, deadline: float, profile: str):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(2)
    try:
        while time.monotonic() < deadline:
            try:
                sock.connect(str(path))
                break
            except (ConnectionRefusedError, FileNotFoundError):
                time.sleep(0.1)
        else:
            prefix = 'QMP socket unavailable' if profile == 'h14' else 'QEMU QMP socket unavailable:'
            raise RuntimeError(f'{prefix} {path}')
        reader = sock.makefile('rb')
        try:
            greeting = qmp['read_json'](reader, time.monotonic() + 10)
            if 'QMP' not in greeting:
                if profile == 'h14':
                    raise RuntimeError(f'QMP greeting absent {greeting!r}')
                raise RuntimeError(f'missing true QMP greeting: {greeting}')
            qmp['qmp_command'](sock, reader, 'qmp_capabilities')
        except BaseException:
            reader.close()
            raise
        return sock, reader
    except BaseException:
        sock.close()
        raise


def require_usb_mouse(sock, reader, profile):
    mice = qmp['qmp_command'](sock, reader, 'query-mice')
    if not isinstance(mice, list):
        prefix = 'query-mice returned' if profile == 'h14' else 'query-mice unexpected return'
        raise RuntimeError(f'{prefix} {mice!r}')
    selected = [m for m in mice if isinstance(m, dict) and
                m.get('name') == 'QEMU HID Mouse' and
                m.get('current') is True and m.get('absolute') is False]
    if len(selected) != 1:
        prefix = ('QEMU did not select unique relative usb-mouse' if profile == 'h14'
                  else 'QMP did not enumerate unique relative USB mouse')
        raise RuntimeError(f'{prefix} {mice!r}')


def key_event(down: bool):
    return {
        'type': 'key',
        'data': {'down': down,
                 'key': {'type': 'qcode', 'data': 'a'}},
    }


def mouse_event(stage: str):
    if stage == 'x':
        return {'type': 'rel', 'data': {'axis': 'x', 'value': 20}}
    if stage == 'y':
        return {'type': 'rel', 'data': {'axis': 'y', 'value': -12}}
    if stage in ('down', 'up'):
        return {'type': 'btn',
                'data': {'button': 'left', 'down': stage == 'down'}}
    raise ValueError(stage)



def parse_keyboard(raw: bytes):
    text = raw.decode('utf-8', errors='replace').replace('\r', '')
    lines = text.splitlines()
    # Locate only original evtest verbose output lines, never pass markers
    # printed by scripts, CPO blobs, or kernel debug instrumentation.
    records = []
    for index, line in enumerate(lines):
        match = H12_KEY_PATTERN.search(line)
        if match:
            records.append((index, int(match.group(1))))
    return lines, records


def seen_key_syn(serial: Path, value: int, previous_index: int,
                 deadline: float) -> int:
    while time.monotonic() < deadline:
        raw = snapshot(serial)
        no_fatal(raw, 'h12')
        lines, records = parse_keyboard(raw)
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


def parse_mouse(raw: bytes):
    lines = raw.decode('utf-8', errors='replace').replace('\r', '').splitlines()
    records = []
    for index, line in enumerate(lines):
        match = H13_EVENT_PATTERN.search(line)
        if match:
            records.append((index, int(match.group(1)),
                            int(match.group(3)), int(match.group(5))))
    return lines, records


def seen_mouse_syn(serial: Path, stage: str, previous_index: int,
                   deadline: float) -> int:
    expected_type, expected_code, expected_value = {
        'x': (2, 0, None),
        'y': (2, 1, None),
        'down': (1, 272, 1),
        'up': (1, 272, 0),
    }[stage]
    while time.monotonic() < deadline:
        raw = snapshot(serial)
        no_fatal(raw, 'h13')
        lines, entries = parse_mouse(raw)
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



def parse_lines(serial):
    return snapshot(serial).decode('utf-8', errors='replace').replace('\r','').splitlines()


def seen_dual_syn(serial, kind, value, after_line, deadline):
    """Read ONLY original evtest lines, not a guest success marker."""
    while time.monotonic() < deadline:
        raw = snapshot(serial)
        no_fatal(raw, 'h14')
        lines = raw.decode('utf-8', errors='replace').replace('\r', '').splitlines()
        for index in range(after_line+1, len(lines)):
            line = lines[index]
            match = H14_KEY_PATTERN.search(line) if kind == 'key' else H14_MOUSE_PATTERN.search(line)
            if match is None:
                continue
            if kind == 'key':
                if int(match.group(1)) != value:
                    continue
            else:
                evtype, _evname, code, _code_name, evvalue = match.groups()
                target_type, target_code, target_value = {
                    'x':(2,0,None), 'y':(2,1,None),
                    'down':(1,272,1), 'up':(1,272,0),
                }[kind]
                if (int(evtype), int(code)) != (target_type, target_code):
                    continue
                if target_value is None and int(evvalue) == 0:
                    continue
                if target_value is not None and int(evvalue) != target_value:
                    continue
            if any(x.startswith('Event: time ') and 'SYN_REPORT' in x
                   for x in lines[index+1:index+8]):
                return index
        time.sleep(.1)
    raise RuntimeError(f'original signed evtest missing {kind}:{value} + SYN_REPORT')


def assert_before(raw, names, markers):
    no_fatal(raw, 'h14')
    for name in names:
        if name not in raw:
            raise RuntimeError(f'original evtest identity missing {name!r}')
    for marker in markers:
        if marker in raw:
            raise RuntimeError(f'QMP stage already present before physical injection {marker!r}')


def run_h12(args, deadline):
    wait_for(args.serial, H12_READY, deadline, 'h12')
    sock, reader = connect_qmp(args.qmp, deadline, 'h12')
    with sock, reader:
        # A PASS is only valid for input injected by *this* live QMP session.
        # Neither historical serial records nor premature input from some
        # other controller may satisfy the hardware test before our first
        # deliberate QMP key-down command.
        before = snapshot(args.serial)
        no_fatal(before, 'h12')
        before_lines, before_records = parse_keyboard(before)
        if before_records or any(marker in before for marker in
                                 (H12_DOWN, H12_UP, H12_GUEST_PASS)):
            raise RuntimeError(
                'evtest key event/success predated the QMP hardware injection')
        baseline_index = len(before_lines) - 1
        qmp['qmp_command'](sock, reader, 'input-send-event',
                           {'events': [key_event(True)]})
        down_index = seen_key_syn(args.serial, 1, baseline_index, deadline)
        # The original evtest --query must independently observe A held
        # BEFORE release, not race against an immediately injected key-up.
        wait_for(args.serial, H12_DOWN, deadline, 'h12')
        qmp['qmp_command'](sock, reader, 'input-send-event',
                           {'events': [key_event(False)]})
        seen_key_syn(args.serial, 0, down_index, deadline)
        wait_for(args.serial, H12_UP, deadline, 'h12')
        wait_for(args.serial, H12_GUEST_PASS, deadline, 'h12')

    print('[H12 QMP REAL USB HID EVTEST PASS] explicit qemu-xhci+usb-kbd physical Key A down/up -> original signed Alpine evtest EV_KEY code 30 value 1 SYN and value 0 SYN', flush=True)


def run_h13(args, deadline):
    wait_for(args.serial, H13_READY, deadline, 'h13')
    sock, reader = connect_qmp(args.qmp, deadline, 'h13')
    with sock, reader:
        before = snapshot(args.serial)
        no_fatal(before, 'h13')
        before_lines, before_records = parse_mouse(before)
        if before_records or H13_GUEST_PASS in before or any(
                token in before for token, _ in H13_STAGES):
            raise RuntimeError('original evtest event or PASS predated QMP injection')
        if (b'Input device ID: bus 0x3' not in before or
                b'Input device name: "QEMU USB HID Boot Mouse"' not in before):
            raise RuntimeError('original evtest did not verify BUS_USB mouse')

        require_usb_mouse(sock, reader, 'h13')

        # Stage each QMP command only after the previous original evtest
        # event was independently observed; PS/2 cannot pass BUS_USB test.
        baseline = len(before_lines) - 1
        for token, stage in H13_STAGES:
            qmp['qmp_command'](sock, reader, 'input-send-event',
                               {'events': [mouse_event(stage)]})
            seen_mouse_syn(args.serial, stage, baseline, deadline)
            wait_for(args.serial, token, deadline, 'h13')
            baseline = len(snapshot(args.serial).replace(b'\r', b'').splitlines()) - 1
        wait_for(args.serial, H13_GUEST_PASS, deadline, 'h13')

    print('[H13 QMP REAL USB MOUSE EVTEST PASS] genuine QEMU USB Mouse REL_X/Y + BTN_LEFT 1/0 each with SYN_REPORT via original signed Alpine BUS_USB evtest', flush=True)


def run_h14(args, deadline):
    # Both original signed Alpine evtest processes must already be OPEN on
    # distinct BUS_USB input devices before any QMP input is deliberately sent.
    wait_for(args.serial, H14_KBD_READY, deadline, 'h14')
    wait_for(args.serial, H14_MOUSE_READY, deadline, 'h14')
    wait_for(args.serial, b'H14_DUAL_READY_FOR_QMP', deadline, 'h14')
    sock, reader=connect_qmp(args.qmp, deadline, 'h14')
    with sock, reader:
        early=snapshot(args.serial)
        assert_before(early, (
            b'[H14 DUAL KEYBOARD ATTACHED] /dev/input/event0',
            b'[H14 DUAL MOUSE ATTACHED] /dev/input/event1',
            b'Input device name: "QEMU USB HID Boot Keyboard"',
            b'Input device name: "QEMU USB HID Boot Mouse"',
        ), (b'H14_KBD_A_DOWN_WITH_SYN',b'H14_KBD_A_UP_WITH_SYN',
            b'H14_MOUSE_REL_X_WITH_SYN',b'H14_MOUSE_REL_Y_WITH_SYN',
            b'H14_MOUSE_BTN_DOWN_WITH_SYN',b'H14_MOUSE_BTN_UP_WITH_SYN',
            H14_GUEST_PASS))
        if early.count(b'Input device ID: bus 0x3')!=2:
            raise RuntimeError('two real original Alpine evtest BUS_USB identity transcripts required')
        if any(H14_KEY_PATTERN.search(line) or H14_MOUSE_PATTERN.search(line)
               for line in parse_lines(args.serial)):
            raise RuntimeError('original evtest event predated deliberate QMP injection')
        require_usb_mouse(sock, reader, 'h14')
        # Interleave across BOTH live xHCI root-port slots. The independent
        # oracle waits for original evtest's own event and SYN for EACH stage.
        sequence=(
            ('key',1,b'H14_KBD_A_DOWN_WITH_SYN',key_event(True)),
            ('x',None,b'H14_MOUSE_REL_X_WITH_SYN',mouse_event('x')),
            ('key',0,b'H14_KBD_A_UP_WITH_SYN',key_event(False)),
            ('y',None,b'H14_MOUSE_REL_Y_WITH_SYN',mouse_event('y')),
            ('down',None,b'H14_MOUSE_BTN_DOWN_WITH_SYN',mouse_event('down')),
            ('up',None,b'H14_MOUSE_BTN_UP_WITH_SYN',mouse_event('up')),
        )
        for kind,value,marker,event in sequence:
            before=snapshot(args.serial)
            no_fatal(before, 'h14')
            if marker in before:
                raise RuntimeError(f'guest stage marker predated QMP injection {marker!r}')
            baseline=len(parse_lines(args.serial))-1
            qmp['qmp_command'](sock,reader,'input-send-event',{'events':[event]})
            seen_dual_syn(args.serial,kind,value,baseline,deadline)
            wait_for(args.serial,marker,deadline, 'h14')
        wait_for(args.serial,H14_GUEST_PASS,deadline, 'h14')
    print('[H14 QMP REAL DUAL USB EVTEST PASS] one QEMU xHCI two concurrent original signed Alpine BUS_USB evtest processes; interleaved KEY_A 1/0, mouse REL_X/Y BTN_LEFT 1/0, independent SYN_REPORT and EVIOCGKEY',flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('profile', choices=('h12', 'h13', 'h14'))
    parser.add_argument('--serial', required=True, type=Path)
    parser.add_argument('--qmp', required=True, type=Path)
    parser.add_argument('--timeout', type=int)
    args = parser.parse_args()
    timeout = args.timeout if args.timeout is not None else (330 if args.profile == 'h14' else 300)
    try:
        {'h12': run_h12, 'h13': run_h13, 'h14': run_h14}[args.profile](
            args, time.monotonic() + timeout)
    except (RuntimeError, OSError, ValueError) as error:
        prefix = {'h12': '[H12 QMP REAL USB HID FAIL]',
                  'h13': '[H13 QMP REAL USB MOUSE FAIL]',
                  'h14': '[H14 QMP REAL DUAL USB EVTEST FAIL]'}[args.profile]
        print(f'{prefix} {error}', file=sys.stderr, flush=True)
        raise SystemExit(1)


if __name__ == '__main__':
    main()
