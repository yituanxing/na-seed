#!/usr/bin/env python3
"""Physical, same-PTY cross-editor lifetime acceptance after M188 Vim.

Only QMP sends input. Original Alpine Nano reopens the file created by Vim,
changes it, writes it, exits and is reaped by the SAME BusyBox shell. Vim
then reopens that modified file. A separate guest shell checker compares bytes.
"""
from pathlib import Path
import runpy
import time

prior = runpy.run_path('scripts/m187-qmp-verify.py', run_name='m189_prior')
m188 = runpy.run_path('scripts/m188-qmp-extras.py', run_name='m189_prior_input')
ui = runpy.run_path('scripts/m173-qmp-verify.py', run_name='m189_prior_screen')
base = runpy.run_path('scripts/m167-qmp-verify.py', run_name='m189_prior_qmp')
qmp = base['qmp']


def frame(sock, reader, outdir, label):
    filename = outdir / f'm189-{label}.ppm'
    filename.unlink(missing_ok=True)
    qmp['qmp_command'](sock, reader, 'screendump',
                       {'filename': str(filename.resolve())})
    ui['pixels'](filename)
    return filename


def process_started(sock, reader, serial, elf, after, seconds):
    marker = f'[EXEC] replaced process with CPIO ELF: {elf}'.encode()
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        raw = serial.read_bytes()
        at = raw.find(marker, after)
        if at >= 0 and raw.find(b'\n', at) >= 0:
            previous = raw.rfind(b'[EXEC] replaced process with ', after, at)
            groups = list(prior['PROCESS_EXEC'].finditer(
                raw, previous if previous >= 0 else after, at))
            if len(groups) != 1:
                prior['fail_native'](sock, reader,
                    f'{elf} process identity ambiguous ({len(groups)} groups)')
            return int(groups[0].group(1)), at + len(marker)
        time.sleep(.1)
    prior['fail_native'](sock, reader, f'{elf} ELF exec not observed')


def process_reaped(sock, reader, serial, name, pid, after, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        tail = serial.read_bytes()[after:]
        exits = [m for m in prior['PROCESS_EXIT'].finditer(tail)
                 if int(m.group(1)) == pid]
        if len(exits) > 1:
            prior['fail_native'](sock, reader, f'{name} exited more than once')
        if exits:
            if int(exits[0].group(2)) != 0:
                prior['fail_native'](sock, reader,
                    f'{name} pid={pid} exited nonzero')
            reaps = [m for m in prior['PROCESS_REAP'].finditer(tail)
                     if int(m.group(1)) == pid and
                     m.start() > exits[0].end()]
            if reaps:
                if len(reaps) != 1 or int(reaps[0].group(2)) != 0:
                    prior['fail_native'](sock, reader,
                        f'{name} pid={pid} wait4 mismatch')
                return
        time.sleep(.1)
    prior['fail_native'](sock, reader,
                         f'{name} pid={pid} never exited and was reaped')


def settled_editor(sock, reader, outdir, before, label, seconds):
    """Physical 53x25 editor screen must differ and be stable twice."""
    previous = None
    stable = 0
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        shot = frame(sock, reader, outdir, label)
        data = ui['pixels'](shot)
        try:
            ui['changed'](before, shot, 309, 199, 615, 410, 800)
            ui['changed'](before, shot, 309, 350, 615, 410, 130)
        except RuntimeError:
            stable = 0
        else:
            if previous is not None:
                delta = 0
                for y in range(199, 410):
                    left, right = (y * 800 + 309) * 3, (y * 800 + 615) * 3
                    delta += sum(a != b for a, b in
                                 zip(previous[left:right], data[left:right]))
                stable = stable + 1 if delta <= 20 else 0
                if stable >= 2:
                    return shot
        previous = data
        time.sleep(.4)
    prior['fail_native'](sock, reader,
                         f'{label} did not display a settled physical editor')


def ctrl(sock, reader, key):
    base['send'](sock, reader, [base['key']('ctrl', True)])
    prior['send_key'](sock, reader, key)
    base['send'](sock, reader, [base['key']('ctrl', False)])


def run(sock, reader, serial: Path, outdir: Path):
    # M188 already saved and checked a native Vim file in this living PTY.
    shell = frame(sock, reader, outdir, 'before-nano')
    after = len(serial.read_bytes())
    m188['keys'](sock, reader, '/usr/bin/nano /tmp/m188-vim.txt')
    prior['send_key'](sock, reader, 'ret')
    nano_pid, nano_start = process_started(
        sock, reader, serial, '/usr/bin/nano', after, 35)
    nano = settled_editor(sock, reader, outdir, shell, 'nano-visible', 25)
    # END -> ENTER appends a second line to Vim's original single line.
    prior['send_key'](sock, reader, 'end')
    prior['send_key'](sock, reader, 'ret')
    m188['keys'](sock, reader, 'm189nanoroundtrip')
    edited = m188['changed_until'](
        sock, reader, outdir, 'nano-typed', nano,
        (309, 199, 615, 410), 100, 15)
    ctrl(sock, reader, 'o')
    m188['changed_until'](
        sock, reader, outdir, 'nano-save-prompt', edited,
        (309, 360, 615, 410), 40, 12)
    prior['send_key'](sock, reader, 'ret')
    time.sleep(.5)
    ctrl(sock, reader, 'x')
    process_reaped(sock, reader, serial, 'Nano', nano_pid, nano_start, 30)
    prior['shell_frame_ready'](sock, reader, outdir, edited,
                                time.monotonic() + 20)

    # Independent checker opens real tmpfs file; no fake editor/file fixture.
    m188['keys'](sock, reader, 'sh /tests/m189-editor-check.sh')
    prior['send_key'](sock, reader, 'ret')
    prior['wait_native_marker'](
        sock, reader, serial, 'M189_NANO_SAVED_FUNCTION_PASS',
        time.monotonic() + 35)
    # Permit the checker shell to return and repaint before a second exec.
    frame(sock, reader, outdir, 'after-byte-check')
    time.sleep(.5)

    # The original Vim must then reopen the Nano-modified file.
    before_vim = frame(sock, reader, outdir, 'before-vim-reopen')
    after = len(serial.read_bytes())
    m188['keys'](sock, reader, '/usr/bin/vim /tmp/m188-vim.txt')
    prior['send_key'](sock, reader, 'ret')
    vim_pid, vim_start = process_started(
        sock, reader, serial, '/usr/bin/vim', after, 35)
    reopened = settled_editor(sock, reader, outdir,
                              before_vim, 'vim-reopened', 25)
    base['send'](sock, reader, [base['key']('shift', True)])
    prior['send_key'](sock, reader, 'semicolon')
    base['send'](sock, reader, [base['key']('shift', False)])
    prior['send_key'](sock, reader, 'q')
    prior['send_key'](sock, reader, 'ret')
    process_reaped(sock, reader, serial, 'reopened Vim',
                   vim_pid, vim_start, 30)
    prior['shell_frame_ready'](sock, reader, outdir, reopened,
                                time.monotonic() + 20)
    m188['keys'](sock, reader, 'echo 189')
    prior['send_key'](sock, reader, 'ret')
    prior['wait_native_marker'](
        sock, reader, serial, 'M189_PTY_SHELL_RESUMED_AFTER_EDITORS',
        time.monotonic() + 20)
    frame(sock, reader, outdir, 'session-finished')
