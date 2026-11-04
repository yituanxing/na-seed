#!/usr/bin/env python3
"""Original Alpine init plus actual GPU, evdev and original Vim in ONE QEMU.

Reuses M167/M187/M188 physical QMP, screenshot, VT100 and keyboard helpers;
only the observed ELF provenance changes from the old CPIO to ext4 `/`.
The real Vim writes the persistent file. Neither this Python host controller
nor any host-side disk utility synthesizes its contents.
"""
import argparse
import os
from pathlib import Path
import runpy
import sys
import time


prior = runpy.run_path('scripts/m187-qmp-verify.py', run_name='m198_prior_qmp')
m188 = runpy.run_path('scripts/m188-qmp-extras.py', run_name='m198_prior_input')
ui = runpy.run_path('scripts/m173-qmp-verify.py', run_name='m198_prior_pixels')
base = runpy.run_path('scripts/m167-qmp-verify.py', run_name='m198_prior_evdev')


def original_ext4_process(sock, reader, serial: Path, elf: str,
                          after: int, seconds: int) -> tuple[int, int]:
    """Pin a real guest PID to an ELF loaded from *Alpine ext4*, not CPIO."""
    marker = f'[EXEC] replaced process with lwext4 ELF: {elf}'.encode()
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        data = serial.read_bytes() if serial.is_file() else b''
        offset = data.find(marker, after)
        if offset >= 0 and b'\n' in data[offset:]:
            previous = data.rfind(b'[EXEC] replaced process with ', after, offset)
            groups = list(prior['PROCESS_EXEC'].finditer(
                data, previous if previous >= 0 else after, offset))
            if len(groups) != 1:
                prior['fail_native'](sock, reader,
                    f'{elf}: missing or ambiguous real thread-group PID')
            return int(groups[0].group(1)), offset + len(marker)
        time.sleep(.1)
    prior['fail_native'](sock, reader, f'original ext4 ELF {elf} never ran')
    raise AssertionError('unreachable')


def original_reaped(sock, reader, serial: Path,
                    pid: int, after: int, seconds: int) -> None:
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        raw = serial.read_bytes()[after:]
        exits = [m for m in prior['PROCESS_EXIT'].finditer(raw)
                 if int(m.group(1)) == pid]
        if len(exits) > 1:
            prior['fail_native'](sock, reader, 'original Vim exited twice')
        if exits:
            if int(exits[0].group(2)) != 0:
                prior['fail_native'](sock, reader,
                    f'original Vim PID {pid} exit={exits[0].group(2)!r}')
            reaps = [m for m in prior['PROCESS_REAP'].finditer(raw)
                     if int(m.group(1)) == pid and m.start() > exits[0].end()]
            if len(reaps) == 1 and int(reaps[0].group(2)) == 0:
                return
        time.sleep(.1)
    prior['fail_native'](sock, reader, 'original Vim was not reaped by PTY ash')


def run(serial: Path, qmp: Path, outdir: Path, seconds: int) -> None:
    deadline = time.monotonic() + seconds
    # This comes from the existing compositor, now launched as an ordinary
    # child of a password-authenticated shell under *original* Alpine PID1.
    prior['wait_native_marker'](None, None, serial,
                                'M187_DESKTOP_INITIAL_VISIBLE', deadline)
    sock, reader = base['connect'](qmp, deadline)
    with sock:
        first = prior['screenshot'](sock, reader, outdir, 'm198-original-alpine')
        pixels = ui['pixels'](first)
        if (ui['pixel'](pixels, 20, 20) != (16, 32, 48) or
                ui['pixel'](pixels, 100, 150) != (255, 0, 255)):
            prior['fail_native'](sock, reader,
                'actual 800x600 VirtIO-GPU scanout lacks independent windows')
        prior['wait_native_marker'](sock, reader, serial,
                                   'M171_LVGL_REAL_RENDER_PASS', deadline)
        # Tablet generates real evdev focus; keyboard never feeds serial.
        ui['pointer_click'](sock, reader, 500, 340)
        ui['pointer_release'](sock, reader)
        before = prior['screenshot'](sock, reader, outdir, 'm198-shell')
        log_start = len(serial.read_bytes())
        m188['keys'](sock, reader, '/usr/bin/vim /root/m198-editor.txt')
        prior['send_key'](sock, reader, 'ret')
        pid, observed = original_ext4_process(
            sock, reader, serial, '/usr/bin/vim', log_start, 90)
        editor = m188['vim_ready'](sock, reader, outdir, before, 70)
        prior['send_key'](sock, reader, 'i')
        m188['keys'](sock, reader, 'm198vimrealinput')
        m188['changed_until'](sock, reader, outdir, 'm198-original-vim-typed',
                              editor, (309, 199, 615, 410), 80, 35)
        prior['send_key'](sock, reader, 'esc')
        time.sleep(.7)
        base['send'](sock, reader, [base['key']('shift', True)])
        prior['send_key'](sock, reader, 'semicolon')
        base['send'](sock, reader, [base['key']('shift', False)])
        m188['keys'](sock, reader, 'wq')
        prior['send_key'](sock, reader, 'ret')
        original_reaped(sock, reader, serial, pid, observed, 90)
        # The second QEMU and host debugfs/fsck will independently compare
        # exact file bytes. The screen and real process identity are checked
        # here, before physical Ctrl-F12 returns the original Alpine ash.
        prior['screenshot'](sock, reader, outdir, 'm198-vim-returned')
        base['send'](sock, reader, [base['key']('ctrl', True)])
        base['send'](sock, reader, [base['key']('f12', True)])
        prior['wait_native_marker'](sock, reader, serial,
                                   'M187_DESKTOP_PASS', deadline)
    print('[M198 PHYSICAL QMP PASS] real VirtIO GPU pixels, three evdev '
          'devices, original ext4 Vim ELF, keyboard edit, process exit/wait4 '
          'and ordinary desktop child exit under Alpine PID1', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--serial', type=Path, required=True)
    parser.add_argument('--qmp', type=Path, required=True)
    parser.add_argument('--outdir', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=1250)
    args = parser.parse_args()
    args.outdir.mkdir(parents=True, exist_ok=True)
    try:
        run(args.serial, args.qmp, args.outdir, args.timeout)
    except Exception as exc:
        failure = os.environ.get('NAS_M198_QMP_FAIL_FILE')
        if failure:
            Path(failure).write_text(str(exc) + '\n')
        print('[M198 PHYSICAL QMP FAIL]', exc, file=sys.stderr, flush=True)
        raise SystemExit(1)
