#!/usr/bin/env python3
"""Exercise *original* Alpine PID1/OpenRC across two independent QEMU boots.

Boot one enables the NaSeed desktop using the original rc-update command,
starts/stops/restarts it as a normal service, and lets an independent physical
QMP controller operate real VirtIO GPU/input and the original signed Vim.
Boot two intentionally issues NO rc-service start: OpenRC must auto-start the
desktop from the persisted guest-created runlevel entry.
"""
import importlib.util
import os
from pathlib import Path
import sys


def load(name: str, alias: str):
    path = Path(__file__).with_name(name)
    spec = importlib.util.spec_from_file_location(alias, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"missing original Alpine UART dependency: {name}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


M197 = load('m197-alpine-uart.py', 'm199_m197')

# These are REAL commands sent only after original Alpine getty/login and
# password authentication; no host-mounted writes synthesize rc-update state.
FIRST_COMMANDS = M197.COMMANDS + (
    ("apk info -e imagemagick && apk info -e lynx && apk info -e mc && "
     "apk info -e dialog && apk info -e vim && apk info -e nano && "
     "apk info -e ncurses-terminfo-base; "
     "printf 'M199_SIGNED_GUI_APK_RC=%d\\n' \"$?\"\r",
     'M199_SIGNED_GUI_APK_RC=0', 'signed upstream GUI APK database'),
    ("test -x /usr/bin/naseed-desktop && test -x /usr/bin/vim && "
     "test -x /usr/bin/magick && test -x /etc/init.d/naseed-desktop && "
     "test ! -e /etc/runlevels/default/naseed-desktop; "
     "printf 'M199_UNENABLED_SERVICE_RC=%d\\n' \"$?\"\r",
     'M199_UNENABLED_SERVICE_RC=0', 'desktop not host-preenabled'),
    ("rc-update add naseed-desktop default && "
     "test -L /etc/runlevels/default/naseed-desktop; "
     "printf 'M199_GUEST_RC_UPDATE_RC=%d\\n' \"$?\"\r",
     'M199_GUEST_RC_UPDATE_RC=0', 'guest original rc-update persisted service'),
    ("rc-service naseed-desktop start; "
     "printf 'M199_FIRST_SERVICE_START_RC=%d\\n' \"$?\"\r",
     'M199_FIRST_SERVICE_START_RC=0', 'original OpenRC spawned the compositor'),
    ("n=0; while ! grep -q M171_LVGL_REAL_RENDER_PASS "
     "/var/log/naseed-desktop.log 2>/dev/null; do "
     "n=$((n+1)); if [ \"$n\" -ge 120 ]; then break; fi; "
     "sleep 1; done; "
     "grep -q M187_DESKTOP_INITIAL_VISIBLE /var/log/naseed-desktop.log && "
     "grep -q M171_LVGL_REAL_RENDER_PASS /var/log/naseed-desktop.log && "
     "cat /var/log/naseed-desktop.log; "
     "printf 'M199_FIRST_OPENRC_GUI_RC=%d\\n' \"$?\"\r",
     'M199_FIRST_OPENRC_GUI_RC=0', 'OpenRC stdout proves live GUI/LVGL'),
    ("rc-service naseed-desktop status && "
     "test -s /run/naseed-desktop.pid && "
     "kill -0 \"$(cat /run/naseed-desktop.pid)\"; "
     "printf 'M199_FIRST_REAL_PID_RC=%d\\n' \"$?\"\r",
     'M199_FIRST_REAL_PID_RC=0', 'OpenRC pidfile owns real desktop'),
    ("rc-service naseed-desktop stop; "
     "printf 'M199_SERVICE_STOP_RC=%d\\n' \"$?\"\r",
     'M199_SERVICE_STOP_RC=0', 'OpenRC SIGTERM graceful desktop stop'),
    # rc-service's SIGTERM can return before the compositor's bounded poll
    # reaps child windows and unlinks its socket. Wait for actual cleanup,
    # rather than racing a successful asynchronous OpenRC stop.
    ("n=0; while [ -e /tmp/naseed-desktop.sock ] || "
     "[ -e /run/naseed-desktop.pid ]; do "
     "n=$((n+1)); if [ \"$n\" -ge 15 ]; then break; fi; "
     "sleep 1; done; "
     "test ! -e /tmp/naseed-desktop.sock && "
     "test ! -e /run/naseed-desktop.pid && "
     "! rc-service naseed-desktop status >/dev/null 2>&1; "
     "printf 'M199_STOP_CLEAN_RC=%d\\n' \"$?\"\r",
     'M199_STOP_CLEAN_RC=0', 'desktop socket and pidfile cleaned'),
    ("rc-service naseed-desktop start && "
     "rc-service naseed-desktop status; "
     "printf 'M199_SERVICE_RESTART_FROM_STOP_RC=%d\\n' \"$?\"\r",
     'M199_SERVICE_RESTART_FROM_STOP_RC=0', 'OpenRC restart after full stop'),
    # Old log markers and a still-running old PID must NOT satisfy a restart.
    # Fresh OpenRC PID plus a fresh graphics-ready marker prove a new session.
    ("old_pid=\"$(cat /run/naseed-desktop.pid)\" && "
     ": > /var/log/naseed-desktop.log && "
     "rc-service naseed-desktop restart && "
     "rc-service naseed-desktop status && "
     "new_pid=\"$(cat /run/naseed-desktop.pid)\" && "
     "test -n \"$old_pid\" && test -n \"$new_pid\" && "
     "test \"$old_pid\" != \"$new_pid\" && "
     "kill -0 \"$new_pid\" && "
     "n=0; while ! grep -q M187_DESKTOP_INITIAL_VISIBLE "
     "/var/log/naseed-desktop.log 2>/dev/null; do "
     "n=$((n+1)); if [ \"$n\" -ge 120 ]; then break; fi; "
     "sleep 1; done; "
     "grep -q M187_DESKTOP_INITIAL_VISIBLE /var/log/naseed-desktop.log; "
     "printf 'M199_OPENRC_RESTART_RC=%d\\n' \"$?\"\r",
     'M199_OPENRC_RESTART_RC=0', 'OpenRC restart of real GUI process'),
    ("rc-service naseed-desktop stop && "
     "test ! -e /tmp/naseed-desktop.sock; "
     "printf 'M199_FINAL_STOP_RC=%d\\n' \"$?\"\r",
     'M199_FINAL_STOP_RC=0', 'clean shutdown before next Alpine boot'),
    # A second-boot log proof must NEVER be satisfied by bytes left over
    # from the first boot. All first-boot log evidence was already printed
    # to physical UART and copied to the host before this fresh boundary.
    (": > /var/log/naseed-desktop.log; "
     "printf 'M199_CLEAR_OLD_GUI_LOG_RC=%d\\n' \"$?\"\r",
     'M199_CLEAR_OLD_GUI_LOG_RC=0', 'remove stale first-boot GUI log marker'),
    ("test \"$(cat /root/m198-editor.txt)\" = m198vimrealinput && "
     "test -L /etc/runlevels/default/naseed-desktop; "
     "printf 'M199_FIRST_DURABLE_RC=%d\\n' \"$?\"\r",
     'M199_FIRST_DURABLE_RC=0', 'hardware Vim and guest runlevel persisted'),
    ("sync; printf 'M199_FIRST_SYNC_RC=%d\\n' \"$?\"\r",
     'M199_FIRST_SYNC_RC=0', 'flush original Alpine physical ext4'),
)

# This is the *second* genuine /sbin/init boot. None of these commands start
# the desktop: if its PID/scanout exists it was launched by Alpine OpenRC.
REBOOT_COMMANDS = (
    ("test -L /etc/runlevels/default/naseed-desktop && "
     "rc-service naseed-desktop status && "
     "test -s /run/naseed-desktop.pid && "
     "kill -0 \"$(cat /run/naseed-desktop.pid)\"; "
     "printf 'M199_REBOOT_AUTO_STATUS_RC=%d\\n' \"$?\"\r",
     'M199_REBOOT_AUTO_STATUS_RC=0', 'original OpenRC auto-started desktop'),
    ("n=0; while ! grep -q M187_DESKTOP_INITIAL_VISIBLE "
     "/var/log/naseed-desktop.log 2>/dev/null; do "
     "n=$((n+1)); if [ \"$n\" -ge 120 ]; then break; fi; "
     "sleep 1; done; "
     "grep -q M187_DESKTOP_INITIAL_VISIBLE /var/log/naseed-desktop.log && "
     "cat /var/log/naseed-desktop.log; "
     "printf 'M199_REBOOT_LOG_RC=%d\\n' \"$?\"\r",
     'M199_REBOOT_LOG_RC=0', 'second boot original compositor output'),
    ("apk info -e bash && apk info -e lua5.4 && "
     "apk info -e sqlite && apk info -e git && "
     "apk info -e imagemagick && apk info -e lynx && "
     "apk info -e mc && apk info -e dialog && apk info -e vim && "
     "apk info -e nano && apk info -e ncurses-terminfo-base; "
     "printf 'M199_REBOOT_APK_RC=%d\\n' \"$?\"\r",
     'M199_REBOOT_APK_RC=0', 'signed upstream APK DB retained'),
    ("/bin/bash /root/m197-software-workload.sh --reopen; "
     "printf 'M199_REBOOT_WORKLOAD_RC=%d\\n' \"$?\"\r",
     'M199_REBOOT_WORKLOAD_RC=0', 'original Bash Lua SQLite Git reopen'),
    ("test \"$(cat /root/m198-editor.txt)\" = m198vimrealinput && "
     "test \"$(cat /root/m197-wan.txt)\" = M197_PUBLIC_ALPINE_SOFTWARE_2026; "
     "printf 'M199_REBOOT_DURABLE_RC=%d\\n' \"$?\"\r",
     'M199_REBOOT_DURABLE_RC=0', 'real QMP Vim and signed app bytes survived'),
    ("rc-service naseed-desktop status; "
     "printf 'M199_REBOOT_STILL_RUNNING_RC=%d\\n' \"$?\"\r",
     'M199_REBOOT_STILL_RUNNING_RC=0', 'OpenRC graphics still alive'),
    ("sync; printf 'M199_REBOOT_SYNC_RC=%d\\n' \"$?\"\r",
     'M199_REBOOT_SYNC_RC=0', 'second original Alpine ext4 sync'),
)


def main():
    if len(sys.argv) != 6 or sys.argv[1] not in ('first', 'reboot'):
        raise SystemExit('usage: m199-alpine-uart.py first|reboot RUNNER SERIAL RUNNER_LOG FIFO')
    phase = sys.argv[1]
    first = phase == 'first'
    fail_name = os.environ.get('NAS_M199_QMP_FAIL_FILE')
    pass_name = os.environ.get('NAS_M199_QMP_PASS_FILE')
    if not fail_name or not pass_name:
        raise RuntimeError('M199 requires real physical QMP failure and success gates')
    commands = FIRST_COMMANDS if first else REBOOT_COMMANDS
    # Only the service-stop command (first) or final sync command (reboot)
    # needs to wait for an independent hardware witness. A host sentinel is
    # not a pass condition; QMP pixels and UART markers supply the proof.
    guard_index = (len(M197.COMMANDS) + 6) if first else (len(REBOOT_COMMANDS) - 1)
    seconds = int(os.environ.get(
        'NAS_M199_FIRST_BOOT_SECONDS' if first else 'NAS_M199_REBOOT_SECONDS',
        '2600' if first else '500'))
    M197.M194.run(sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4]),
                  Path(sys.argv[5]), seconds + 35, commands=commands,
                  stage='M199-' + phase.upper(),
                  failure_watch=Path(fail_name),
                  before_command_barriers={guard_index: Path(pass_name)},
                  success_message='[M199 ALPINE OPENRC ' + phase.upper() +
                  ' UART PASS] original Alpine init/OpenRC, genuine pidfile, '
                  'external QMP graphics, original upstream software and ext4')


if __name__ == '__main__':
    main()
