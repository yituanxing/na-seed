#!/usr/bin/env python3
"""Additional *physical* input and signed Alpine Vim acceptance in M187 GUI.

M188 deliberately reuses the same running M187 compositor, PTY, original
signed rootfs and QEMU machine. No second graphics stack, terminal emulator,
filesystem image or testcase-specific replacement editor is introduced.
"""
from pathlib import Path
import re
import runpy
import time

prior = runpy.run_path('scripts/m187-qmp-verify.py',
                       run_name='m188_existing_qmp_library')
ui = runpy.run_path('scripts/m173-qmp-verify.py',
                    run_name='m188_existing_screen_library')
base = runpy.run_path('scripts/m167-qmp-verify.py',
                      run_name='m188_existing_input_library')
qmp = base['qmp']


def frame(sock, reader, outdir, name):
    dst = outdir / f'm188-{name}.ppm'
    dst.unlink(missing_ok=True)
    qmp['qmp_command'](sock, reader, 'screendump',
                       {'filename': str(dst.resolve())})
    ui['pixels'](dst)  # Real QEMU P6 800x600, never a guest-created fixture.
    return dst


def changed_until(sock, reader, outdir, name, previous,
                  rect, minimum, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        current = frame(sock, reader, outdir, name)
        try:
            ui['changed'](previous, current, *rect, minimum)
            return current
        except RuntimeError:
            time.sleep(.25)
    prior['fail_native'](sock, reader,
                         f'M188 {name} lacked actual changed QEMU pixels')


def vim_ready(sock, reader, outdir, before, seconds, *, profile='m188'):
    """Wait for the ORIGINAL editor's settled full-screen frame.

    A post-ELF-exec shell command echo also changes pixels, but is not a
    cursor-addressing editor ready to accept `i`. Require repaint in the
    lower part of the 53x25 PTY (where Vim draws its status and empty-buffer
    marks), then two consecutive stable QEMU scanouts, just as M187 waits
    for a settled GNU dialog before sending the hardware arrow key.

    M198/M199 run the same ORIGINAL Vim from Alpine ext4, but their shell
    and Vim use identical blue backgrounds. The only upper-screen change
    is the `~` gutter at x=300..308, outside M188's legacy x>=309 ROI.
    Their explicit profile therefore requires *both* the gutter and the
    separate bottom status line; a mere shell command echo cannot pass.
    M188's original pixel thresholds remain unchanged.
    """
    def changed_bytes(a, b, left, top, right, bottom):
        return sum(sum(x != y for x, y in zip(
            a[(row * 800 + left) * 3:(row * 800 + right) * 3],
            b[(row * 800 + left) * 3:(row * 800 + right) * 3]))
            for row in range(top, bottom))

    if profile == 'm188':
        first_rect, first_min, first_label = (309, 199, 615, 410), 1000, 'full'
        second_rect, second_min, second_label = (309, 350, 615, 410), 220, 'lower'
        stable_rect = first_rect
        frame_name = 'vim-visible'
    elif profile == 'alpine-ext4':
        first_rect, first_min, first_label = (300, 199, 309, 398), 250, 'tilde'
        second_rect, second_min, second_label = (300, 399, 630, 415), 600, 'status'
        stable_rect = (300, 199, 630, 415)
        frame_name = 'alpine-ext4-vim-visible'
    else:
        raise ValueError(f'unknown Vim QMP screen profile: {profile}')

    deadline = time.monotonic() + seconds
    baseline = ui['pixels'](before)
    previous = None
    stable = 0
    max_stable = 0
    samples = 0
    matching_frames = 0
    last_full = last_lower = last_delta = None
    max_full = max_lower = 0
    min_delta = None
    while time.monotonic() < deadline:
        shot = frame(sock, reader, outdir, frame_name)
        data = ui['pixels'](shot)
        samples += 1
        last_full = changed_bytes(baseline, data, *first_rect)
        last_lower = changed_bytes(baseline, data, *second_rect)
        max_full = max(max_full, last_full)
        max_lower = max(max_lower, last_lower)
        last_delta = (changed_bytes(previous, data, *stable_rect)
                      if previous is not None else None)
        if last_delta is not None:
            min_delta = (last_delta if min_delta is None else
                         min(min_delta, last_delta))
        # Distinct gutter + statusline evidence for ext4; unchanged M188
        # strict full-screen thresholds for its existing CPIO regression.
        if last_full < first_min or last_lower < second_min:
            stable = 0
        else:
            matching_frames += 1
            if last_delta is not None:
                stable = stable + 1 if last_delta <= 20 else 0
                max_stable = max(max_stable, stable)
                if stable >= 2:
                    return shot
        previous = data
        time.sleep(.4)
    print(f'[M188 VIM FRAME DIAGNOSTIC profile={profile}] '
          f'samples={samples} matching_frames={matching_frames} '
          f'{first_label}_last={last_full} {first_label}_max={max_full} '
          f'{first_label}_required={first_min} '
          f'{second_label}_last={last_lower} {second_label}_max={max_lower} '
          f'{second_label}_required={second_min} '
          f'delta_last={last_delta} delta_min={min_delta} '
          f'stable_last={stable} stable_max={max_stable} stable_required=2 '
          'delta_limit=20', flush=True)
    prior['fail_native'](sock, reader,
                         'original Vim did not show a settled physical '
                         'full-screen PTY frame after kernel ELF exec')


def pointer(sock, reader, x, y):
    base['send'](sock, reader, base['pointer'](x, y))
    time.sleep(.35)


def button(sock, reader, down):
    base['send'](sock, reader, [base['button'](down)])
    time.sleep(.3)


def wheel(sock, reader, name):
    # QEMU emits a real VirtIO-input EV_REL wheel event, not a direct
    # invocation of the compositor or client-side LVGL callback.
    for down in (True, False):
        base['send'](sock, reader, [
            {'type': 'btn', 'data': {'button': name, 'down': down}}
        ])
    time.sleep(.35)


def keys(sock, reader, text):
    qcodes = {' ': 'spc', '/': 'slash', '-': 'minus', '.': 'dot'}
    for ch in text:
        prior['send_key'](sock, reader, qcodes.get(ch, ch))


def wait_vim_pid(sock, reader, serial, seconds):
    marker = b'[EXEC] replaced process with CPIO ELF: /usr/bin/vim'
    prior['wait_native_marker'](
        sock, reader, serial, marker.decode('ascii'),
        time.monotonic() + seconds)
    raw = serial.read_bytes()
    at = raw.rfind(marker)
    previous = raw.rfind(b'[EXEC] replaced process with ', 0, at)
    group = list(prior['PROCESS_EXEC'].finditer(
        raw, previous if previous >= 0 else 0, at))
    if len(group) != 1:
        prior['fail_native'](sock, reader,
                             f'original Vim process identity ambiguous: '
                             f'{len(group)} exec groups')
    return int(group[0].group(1)), at


def wait_vim_saved_exit(sock, reader, serial, pid, after, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        tail = serial.read_bytes()[after:]
        exits = [m for m in prior['PROCESS_EXIT'].finditer(tail)
                 if int(m.group(1)) == pid]
        if exits:
            if len(exits) != 1 or int(exits[0].group(2)) != 0:
                prior['fail_native'](sock, reader,
                                     'original Vim did not exit cleanly')
            reaps = [m for m in prior['PROCESS_REAP'].finditer(tail)
                     if int(m.group(1)) == pid and
                        m.start() > exits[0].end()]
            if reaps:
                if len(reaps) != 1 or int(reaps[0].group(2)) != 0:
                    prior['fail_native'](sock, reader,
                                         'original Vim wait4 status mismatch')
                return
        time.sleep(.1)
    prior['fail_native'](sock, reader,
                         'original Vim never wrote and returned to shell')


def run(sock, reader, serial: Path, outdir: Path):
    # M187's four ORIGINAL boot clients include LVGL at (75,365) with a
    # 24-pixel titlebar, hence a content origin of (75,389).  The dynamic
    # extra LVGL window spawned by M173's separate `gui` command uses an
    # entirely different (535,370) position and is NOT launched here.
    # Coordinates below therefore exercise the actual M187 boot client.
    # 1. Monotonic LVGL timers must drive actual pixels without input events.
    initial = frame(sock, reader, outdir, 'lvgl-time-initial')
    prior['wait_native_marker'](
        sock, reader, serial, 'M188_LVGL_TIMER_PASS',
        time.monotonic() + 12)
    changed_until(sock, reader, outdir, 'lvgl-time-progress', initial,
                  (86, 415, 190, 442), 15, 12)

    # 2. One physical button hold + several absolute tablet positions must
    # move the original LVGL slider, not drag the compositor titlebar.
    pointer(sock, reader, 103, 535)
    button(sock, reader, True)
    at_left = frame(sock, reader, outdir, 'slider-held')
    pointer(sock, reader, 201, 535)
    moved = changed_until(sock, reader, outdir, 'slider-dragged', at_left,
                          (90, 512, 215, 572), 90, 12)
    button(sock, reader, False)
    prior['wait_native_marker'](
        sock, reader, serial, 'M188_LVGL_SLIDER_DRAG_PASS',
        time.monotonic() + 12)

    # 3. Physical wheel drives LVGL's scrollable surface. This must produce
    # both a kernel input event and actual new shared-surface scanout pixels.
    pointer(sock, reader, 260, 464)
    before_wheel = frame(sock, reader, outdir, 'scroll-before')
    wheel(sock, reader, 'wheel-down')
    prior['wait_native_marker'](
        sock, reader, serial, 'M188_LVGL_SCROLL_PASS',
        time.monotonic() + 12)
    changed_until(sock, reader, outdir, 'scroll-after', before_wheel,
                  (226, 424, 306, 544), 50, 12)

    # 4. Refocus the same live PTY that already ran original Alpine dialog.
    # The original Alpine Vim must edit a NEW file from real keyboard input.
    ui['pointer_click'](sock, reader, 500, 340)
    ui['pointer_release'](sock, reader)
    terminal_before = frame(sock, reader, outdir, 'before-vim')
    keys(sock, reader, '/usr/bin/vim /tmp/m188-vim.txt')
    prior['send_key'](sock, reader, 'ret')
    pid, started = wait_vim_pid(sock, reader, serial, 35)
    editor = vim_ready(sock, reader, outdir, terminal_before, 25)
    prior['send_key'](sock, reader, 'i')
    keys(sock, reader, 'm188vimrealinput')
    inserted = changed_until(sock, reader, outdir, 'vim-inserted', editor,
                             (309, 199, 615, 410), 80, 15)
    prior['send_key'](sock, reader, 'esc')
    time.sleep(1.2)
    base['send'](sock, reader, [base['key']('shift', True)])
    prior['send_key'](sock, reader, 'semicolon')  # ':'
    base['send'](sock, reader, [base['key']('shift', False)])
    keys(sock, reader, 'wq')
    prior['send_key'](sock, reader, 'ret')
    wait_vim_saved_exit(sock, reader, serial, pid, started, 30)

    # A distinct checker reopens the persisted tmpfs file after Vim exits.
    # Its literal result token does not appear in this typed command.
    keys(sock, reader, 'sh /tests/m188-editor-check.sh')
    prior['send_key'](sock, reader, 'ret')
    prior['wait_native_marker'](
        sock, reader, serial, 'M188_VIM_SAVED_FUNCTION_PASS',
        time.monotonic() + 30)
    changed_until(sock, reader, outdir, 'vim-shell-verified', inserted,
                  (309, 199, 615, 410), 180, 15)
