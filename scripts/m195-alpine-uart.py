#!/usr/bin/env python3
"""Install an original signed Alpine APK over genuine QEMU VirtIO-Net.

Reuses M194's real getty/password/ash UART state machine. No guest-side
acceptance executable, synthetic login, or preinstalled test package exists.
The host wire oracle and second physical boot are checked by the shell gate.
"""

import importlib.util
from pathlib import Path
import sys


def load_m194():
    path = Path(__file__).with_name("m194-alpine-uart.py")
    spec = importlib.util.spec_from_file_location("naseed_m194_uart", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("M194 real Alpine UART driver is missing")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


M194 = load_m194()
WITNESS = "M195_SIGNED_ALPINE_WIRE_2026"
COMMANDS = (
    ("if apk info -e tree >/dev/null 2>&1; then printf 'M195_TREE_PREEXISTING\\n'; "
     "else printf 'M195_TREE_ABSENT\\n'; fi\r",
     "M195_TREE_ABSENT", "new package not installed in signed baseline"),
    ("apk --no-cache --repositories-file /root/m195-repositories add tree; "
     "printf 'M195_APK_ADD_RC=%d\\n' \"$?\"\r",
     "M195_APK_ADD_RC=0", "original apk download and signed installation"),
    ("apk info -e tree >/dev/null; printf 'M195_APK_DB_RC=%d\\n' \"$?\"\r",
     "M195_APK_DB_RC=0", "guest package database updated"),
    ("test -x /usr/bin/tree && /usr/bin/tree --version >/dev/null; "
     "printf 'M195_TREE_ELF_RC=%d\\n' \"$?\"\r",
     "M195_TREE_ELF_RC=0", "new original Alpine ELF executed"),
    ("rc-service crond start; printf 'M195_CROND_RC=%d\\n' \"$?\"\r",
     "M195_CROND_RC=0", "OpenRC service while real network is active"),
    ("wget -q -O /root/m195-wire.txt http://10.0.2.2:15417/proof.txt; "
     "printf 'M195_HTTP_RC=%d\\n' \"$?\"\r",
     "M195_HTTP_RC=0", "real VirtIO-Net HTTP response persisted in ext4"),
    ("cat /root/m195-wire.txt\r", WITNESS,
     "exact host-supplied network payload read through Alpine rootfs"),
    ("sync; printf 'M195_SYNC_RC=%d\\n' \"$?\"\r",
     "M195_SYNC_RC=0", "actual guest ext4 sync after package and HTTP write"),
)


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: m195-alpine-uart.py RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    import os
    seconds = int(os.environ.get("NAS_M195_DIAG_SECONDS", "210"))
    M194.run(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]),
             Path(sys.argv[4]), seconds + 35, commands=COMMANDS, stage="M195",
             success_message="[M195 ALPINE ONLINE APK PASS] actual root PID1 -> "
                             "OpenRC -> password login -> signed HTTP APK "
                             "installation -> new native ELF -> live crond "
                             "-> HTTP payload -> ext4 sync; second boot pending")


if __name__ == "__main__":
    main()
