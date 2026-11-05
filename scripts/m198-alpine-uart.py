#!/usr/bin/env python3
"""Original Alpine PID1 + prior genuine M197 workload + one GUI process.

The graphical client is a *child* of the authenticated, original Alpine ash,
not a replacement init or a second test-only root filesystem. A separate
QMP controller operates physical VirtIO devices while ash waits for the GUI.
"""
import importlib.util
import os
from pathlib import Path
import sys


def load_m197():
    path = Path(__file__).with_name("m197-alpine-uart.py")
    spec = importlib.util.spec_from_file_location("m198_m197_uart", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("M198 requires the unchanged M197 UART workload")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


M197 = load_m197()
COMMANDS = M197.COMMANDS + (
    ("apk info -e vim && apk info -e nano && "
     "apk info -e ncurses-terminfo-base; "
     "printf 'M198_PUBLIC_GUI_APK_RC=%d\\n' \"$?\"\r",
     "M198_PUBLIC_GUI_APK_RC=0",
     "original signed Vim/Nano installed in disposable official root"),
    ("test -x /usr/bin/vim && test -x /usr/bin/nano && "
     "test -x /usr/bin/naseed-desktop && "
     "test -s /tests/m167-desktop-fixture.ppm && "
     "test -s /tests/m168-home.html; "
     "printf 'M198_DESKTOP_INPUTS_RC=%d\\n' \"$?\"\r",
     "M198_DESKTOP_INPUTS_RC=0",
     "original package programs and separately built NaSeed clients"),
    ("NASEED_TERMINAL_VT100=1 /usr/bin/naseed-desktop m187; "
     "printf 'M198_DESKTOP_EXIT_RC=%d\\n' \"$?\"\r",
     "M198_DESKTOP_EXIT_RC=0",
     "existing userspace desktop closed after physical QMP Ctrl-F12"),
    ("test \"$(cat /root/m198-editor.txt)\" = "
     "m198vimrealinput; "
     "printf 'M198_GUI_DURABLE_RC=%d\\n' \"$?\"\r",
     "M198_GUI_DURABLE_RC=0",
     "original Alpine root shell independently reads QMP-driven PTY write"),
    ("sync; printf 'M198_FINAL_SYNC_RC=%d\\n' \"$?\"\r",
     "M198_FINAL_SYNC_RC=0", "physical root ext4 flushed after GUI"),
)


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: m198-alpine-uart.py RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    # The original M197 public APK transaction and M198 original editors
    # run before the GUI, so this is the entire FIRST boot, not a GUI timer.
    seconds = int(os.environ.get("NAS_M198_FIRST_BOOT_SECONDS", "2400"))
    fail_file = os.environ.get('NAS_M198_QMP_FAIL_FILE')
    M197.M194.run(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]),
                  Path(sys.argv[4]), seconds + 35, commands=COMMANDS,
                  stage="M198", failure_watch=Path(fail_file) if fail_file else None,
                  success_message="[M198 ALPINE GUI UART PASS] "
                  "original /sbin/init -> OpenRC/login -> public signed APK "
                  "-> real QMP keyboard original Vim -> ext4 guest readback")


if __name__ == "__main__":
    main()
