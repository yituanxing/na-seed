#!/usr/bin/env python3
"""Physical VirtIO acceptance for one original Alpine OpenRC GUI service.

Two different system-QEMU boots share the genuine Alpine ext4 disk.  The
first edits a file using the *original* signed Vim from the OpenRC-managed
desktop's PTY, and deliberately leaves the GUI running so OpenRC owns its
subsequent stop.  The second verifies OpenRC's automatic startup using real
framebuffer pixels and genuine QMP tablet/keyboard events.  This host script
never creates or changes guest files and never substitutes for OpenRC.
"""

import argparse
import os
from pathlib import Path
import runpy
import sys
import time


old = runpy.run_path('scripts/m198-qmp-alpine-desktop.py',
                     run_name='m199_reused_alpine_qmp')
prior = old['prior']
m188 = old['m188']
ui = old['ui']
base = old['base']


def wait_original_openrc(serial: Path, mode: str, seconds: int) -> float:
    # The original ash checks the actual OpenRC status after the desktop's
    # own stdout/stderr have been captured and re-printed to real UART.  A
    # command receipt alone is not a successful graphical service.
    expected = ('M199_FIRST_OPENRC_GUI_RC=0' if mode == 'first' else
                'M199_REBOOT_AUTO_STATUS_RC=0')
    # Public HTTPS APK transactions can occupy most of boot one. The QMP
    # budget starts only when the native OpenRC GUI is visibly ready, while
    # this pre-GUI limit follows the owning boot's full native QEMU budget.
    default_preparation = ('2600' if mode == 'first' else '500')
    preparation = time.monotonic() + int(
        os.environ.get('NAS_M199_PRE_GUI_SECONDS', default_preparation))
    prior['wait_native_marker'](None, None, serial, expected, preparation)
    deadline = time.monotonic() + seconds
    prior['wait_native_marker'](None, None, serial,
                                'M187_DESKTOP_INITIAL_VISIBLE', deadline)
    prior['wait_native_marker'](None, None, serial,
                                'M171_LVGL_REAL_RENDER_PASS', deadline)
    return deadline


def check_native_gpu(sock, reader, outdir: Path, name: str) -> Path:
    frame = prior['screenshot'](sock, reader, outdir, 'm199-' + name)
    pixels = ui['pixels'](frame)
    if (ui['pixel'](pixels, 20, 20) != (16, 32, 48) or
            ui['pixel'](pixels, 100, 150) != (255, 0, 255)):
        prior['fail_native'](sock, reader,
            'OpenRC original Alpine lacks independent real VirtIO-GPU windows')
    return frame


def first_boot(sock, reader, serial: Path, outdir: Path) -> None:
    # Identical GUI/PTY/original ELF provenance and physical image oracle to
    # M198; no independent editor bootstrap or weaker simulated callback.
    ui['pointer_click'](sock, reader, 500, 340)
    ui['pointer_release'](sock, reader)
    before = prior['screenshot'](sock, reader, outdir, 'm199-shell-before-vim')
    log_start = len(serial.read_bytes())
    m188['keys'](sock, reader, '/usr/bin/vim /root/m198-editor.txt')
    prior['send_key'](sock, reader, 'ret')
    pid, observed = old['original_ext4_process'](
        sock, reader, serial, '/usr/bin/vim', log_start, 90)
    editor = m188['vim_ready'](sock, reader, outdir, before, 70,
                               profile='alpine-ext4')
    prior['send_key'](sock, reader, 'i')
    m188['keys'](sock, reader, 'm198vimrealinput')
    m188['changed_until'](sock, reader, outdir, 'm199-original-vim-typed',
                          editor, (309, 199, 615, 410), 80, 35)
    prior['send_key'](sock, reader, 'esc')
    time.sleep(.7)
    base['send'](sock, reader, [base['key']('shift', True)])
    prior['send_key'](sock, reader, 'semicolon')
    base['send'](sock, reader, [base['key']('shift', False)])
    m188['keys'](sock, reader, 'wq')
    prior['send_key'](sock, reader, 'ret')
    old['original_reaped'](sock, reader, serial, pid, observed, 90)
    prior['screenshot'](sock, reader, outdir, 'm199-openrc-vim-returned')
    # Deliberately NO physical Ctrl+F12 here: OpenRC must stop and reap the
    # still-running compositor and all children in a subsequent guest step.


def reboot_auto_boot(sock, reader, outdir: Path) -> None:
    # QMP pointer selection must reach the newly OpenRC-spawned terminal; a
    # physical keyboard character must visibly alter *that* fresh scanout.
    ui['pointer_click'](sock, reader, 500, 340)
    ui['pointer_release'](sock, reader)
    before = prior['screenshot'](sock, reader, outdir, 'm199-reboot-shell')
    prior['send_key'](sock, reader, 'a')
    m188['changed_until'](sock, reader, outdir, 'm199-reboot-evdev-key',
                          before, (309, 199, 615, 410), 30, 25)
    prior['screenshot'](sock, reader, outdir, 'm199-reboot-keyboard-visible')
    # Do not terminate the graphic service; the reboot oracle verifies that
    # OpenRC owns it, rather than a host terminal automation child process.


def run(mode: str, serial: Path, qmp: Path, outdir: Path,
        success_file: Path, seconds: int) -> None:
    if success_file.exists():
        raise RuntimeError('refusing stale physical-QMP success sentinel')
    deadline = wait_original_openrc(serial, mode, seconds)
    sock, reader = base['connect'](qmp, deadline)
    with sock:
        check_native_gpu(sock, reader, outdir, mode + '-original-alpine')
        if mode == 'first':
            first_boot(sock, reader, serial, outdir)
            marker = 'M199_FIRST_PHYSICAL_QMP_PASS'
        else:
            reboot_auto_boot(sock, reader, outdir)
            marker = 'M199_REBOOT_PHYSICAL_QMP_PASS'
    # A host-only sentinel allows the owning UART driver to advance without
    # forging any data in the guest's real ext4 root or replacing real pixels.
    success_file.parent.mkdir(parents=True, exist_ok=True)
    success_file.write_text(marker + '\n')
    print(f'[{marker}] original Alpine OpenRC service with real QMP '
          'GPU/tablet/keyboard, no host-created guest file', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mode', choices=['first', 'reboot'], required=True)
    parser.add_argument('--serial', type=Path, required=True)
    parser.add_argument('--qmp', type=Path, required=True)
    parser.add_argument('--outdir', type=Path, required=True)
    parser.add_argument('--success-file', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=300)
    args = parser.parse_args()
    args.outdir.mkdir(parents=True, exist_ok=True)
    try:
        run(args.mode, args.serial, args.qmp, args.outdir,
            args.success_file, args.timeout)
    except Exception as exc:
        failure = os.environ.get('NAS_M199_QMP_FAIL_FILE')
        if failure:
            Path(failure).write_text(str(exc) + '\n')
        print(f'[M199 {args.mode.upper()} PHYSICAL QMP FAIL]', exc,
              file=sys.stderr, flush=True)
        raise SystemExit(1)
