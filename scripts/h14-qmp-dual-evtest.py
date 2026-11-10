#!/usr/bin/env python3
"""Independent H14 QMP oracle: one real QEMU xHCI, two simultaneously attached
USB root-port devices, TWO concurrent original signed Alpine evtest processes on event0 and event1.

A real QMP command is insufficient to PASS. Each key or relative mouse event
must appear in original evtest output, followed by SYN_REPORT; both guest
EVIOCGKEY hold/release queries and BUS_USB device identities are required.
"""
import argparse
from pathlib import Path
import re
import runpy
import socket
import sys
import time

qmp = runpy.run_path('scripts/m161-qmp-verify.py', run_name='h14_qmp_library')
KBD_READY = b'H14_KBD_READY_FOR_QMP'
MOUSE_READY = b'H14_MOUSE_READY_FOR_QMP'
GUEST_PASS = b'H14_ORIGINAL_ALPINE_DUAL_EVTEST_USB_PASS'
FATALS = (b'[H14 DUAL FAIL', b'[H14 XHCI FAIL', b'[FAIL]', b'[FAULT]',
          b'[EXEC REJECT]', b'[ELF REJECT]', b'H14_DUAL_EVTEST_FAIL_STAGE_')
KEY_PATTERN = re.compile(r'^Event: time .*type 1 \(EV_KEY\), code 30 \(KEY_A\), value ([01])\b')
MOUSE_PATTERN = re.compile(
    r'^Event: time .*type ([12]) \((EV_KEY|EV_REL)\), '
    r'code (\d+) \((BTN_LEFT|REL_X|REL_Y)\), value (-?\d+)\b'
)


def snapshot(path):
    return path.read_bytes() if path.is_file() else b''


def no_fatal(raw):
    for marker in FATALS:
        if marker in raw:
            raise RuntimeError(f'guest fatal marker {marker!r}')


def wait_for(serial, token, deadline):
    while time.monotonic() < deadline:
        raw = snapshot(serial)
        no_fatal(raw)
        if raw.count(token) > 1:
            raise RuntimeError(f'duplicate guest marker {token!r}')
        if token in raw:
            return raw
        time.sleep(.1)
    raise RuntimeError(f'guest timeout {token!r}')


def connect_qmp(path, deadline):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(2)
    while time.monotonic() < deadline:
        try:
            sock.connect(str(path))
            break
        except (ConnectionRefusedError, FileNotFoundError):
            time.sleep(.1)
    else:
        sock.close()
        raise RuntimeError(f'QMP socket unavailable {path}')
    reader = sock.makefile('rb')
    greeting = qmp['read_json'](reader, time.monotonic()+10)
    if 'QMP' not in greeting:
        raise RuntimeError(f'QMP greeting absent {greeting!r}')
    qmp['qmp_command'](sock, reader, 'qmp_capabilities')
    return sock, reader


def key_event(down):
    return {'type': 'key',
            'data': {'down': down, 'key': {'type':'qcode', 'data':'a'}}}


def mouse_event(stage):
    if stage == 'x':
        return {'type':'rel', 'data':{'axis':'x', 'value':20}}
    if stage == 'y':
        return {'type':'rel', 'data':{'axis':'y', 'value':-12}}
    if stage in ('down','up'):
        return {'type':'btn',
                'data':{'button':'left','down':stage=='down'}}
    raise ValueError(stage)


def parse_lines(serial):
    return snapshot(serial).decode('utf-8', errors='replace').replace('\r','').splitlines()


def seen_event_syn(serial, kind, value, after_line, deadline):
    """Read ONLY original evtest lines, not a guest success marker."""
    while time.monotonic() < deadline:
        raw = snapshot(serial)
        no_fatal(raw)
        lines = raw.decode('utf-8', errors='replace').replace('\r', '').splitlines()
        for index in range(after_line+1, len(lines)):
            line = lines[index]
            match = KEY_PATTERN.search(line) if kind == 'key' else MOUSE_PATTERN.search(line)
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
    no_fatal(raw)
    for name in names:
        if name not in raw:
            raise RuntimeError(f'original evtest identity missing {name!r}')
    for marker in markers:
        if marker in raw:
            raise RuntimeError(f'QMP stage already present before physical injection {marker!r}')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--serial', required=True, type=Path)
    parser.add_argument('--qmp', required=True, type=Path)
    parser.add_argument('--timeout', type=int, default=330)
    args=parser.parse_args()
    deadline=time.monotonic()+args.timeout
    # Both original signed Alpine evtest processes must already be OPEN on
    # distinct BUS_USB input devices before any QMP input is deliberately sent.
    wait_for(args.serial, KBD_READY, deadline)
    wait_for(args.serial, MOUSE_READY, deadline)
    wait_for(args.serial, b'H14_DUAL_READY_FOR_QMP', deadline)
    sock, reader=connect_qmp(args.qmp, deadline)
    with sock:
        early=snapshot(args.serial)
        assert_before(early, (
            b'[H14 DUAL KEYBOARD ATTACHED] /dev/input/event0',
            b'[H14 DUAL MOUSE ATTACHED] /dev/input/event1',
            b'Input device name: "QEMU USB HID Boot Keyboard"',
            b'Input device name: "QEMU USB HID Boot Mouse"',
        ), (b'H14_KBD_A_DOWN_WITH_SYN',b'H14_KBD_A_UP_WITH_SYN',
            b'H14_MOUSE_REL_X_WITH_SYN',b'H14_MOUSE_REL_Y_WITH_SYN',
            b'H14_MOUSE_BTN_DOWN_WITH_SYN',b'H14_MOUSE_BTN_UP_WITH_SYN',
            GUEST_PASS))
        if early.count(b'Input device ID: bus 0x3')!=2:
            raise RuntimeError('two real original Alpine evtest BUS_USB identity transcripts required')
        if any(KEY_PATTERN.search(line) or MOUSE_PATTERN.search(line)
               for line in parse_lines(args.serial)):
            raise RuntimeError('original evtest event predated deliberate QMP injection')
        mice=qmp['qmp_command'](sock,reader,'query-mice')
        if not isinstance(mice,list):
            raise RuntimeError(f'query-mice returned {mice!r}')
        chosen=[m for m in mice if isinstance(m,dict)
                and m.get('name')=='QEMU HID Mouse'
                and m.get('current') is True
                and m.get('absolute') is False]
        if len(chosen)!=1:
            raise RuntimeError(f'QEMU did not select unique relative usb-mouse {mice!r}')
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
            no_fatal(before)
            if marker in before:
                raise RuntimeError(f'guest stage marker predated QMP injection {marker!r}')
            baseline=len(parse_lines(args.serial))-1
            qmp['qmp_command'](sock,reader,'input-send-event',{'events':[event]})
            seen_event_syn(args.serial,kind,value,baseline,deadline)
            wait_for(args.serial,marker,deadline)
        wait_for(args.serial,GUEST_PASS,deadline)
    print('[H14 QMP REAL DUAL USB EVTEST PASS] one QEMU xHCI two concurrent original signed Alpine BUS_USB evtest processes; interleaved KEY_A 1/0, mouse REL_X/Y BTN_LEFT 1/0, independent SYN_REPORT and EVIOCGKEY',flush=True)


if __name__=='__main__':
    try:
        main()
    except (RuntimeError,OSError,ValueError) as exc:
        print(f'[H14 QMP REAL DUAL USB EVTEST FAIL] {exc}',file=sys.stderr,flush=True)
        raise SystemExit(1)
