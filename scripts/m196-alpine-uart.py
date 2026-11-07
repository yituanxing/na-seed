#!/usr/bin/env python3
"""Real Alpine root PID1 -> public DNS -> verified HTTPS APK -> ext4.

Reuse the M194 login and original Alpine ash UART state machine. M195's
deterministic host-only package oracle remains an independent baseline.
"""

import importlib.util
import os
from pathlib import Path
import sys


def load_m195():
    path = Path(__file__).with_name("m195-alpine-uart.py")
    spec = importlib.util.spec_from_file_location("naseed_m195_uart", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("M195 Alpine UART adapter unavailable")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


M194 = load_m195().M194
WITNESS = "M196_PUBLIC_ALPINE_APK_2026"
COMMANDS = (
    ("test -s /etc/ssl/cert.pem && grep -q 'nameserver 10.0.2.3' "
     "/etc/resolv.conf; printf 'M196_TRUST_DNS_RC=%d\\n' \"$?\"\r",
     "M196_TRUST_DNS_RC=0", "original Alpine CA and QEMU DNS configuration"),
    ("if apk info -e tree >/dev/null 2>&1 || apk info -e jq >/dev/null 2>&1; "
     "then printf 'M196_PACKAGES_PREEXISTING\\n'; "
     "else printf 'M196_PACKAGES_ABSENT\\n'; fi\r",
     "M196_PACKAGES_ABSENT", "new programs absent from the signed baseline"),
    ("apk --no-cache add tree jq; printf 'M196_PUBLIC_APK_RC=%d\\n' \"$?\"\r",
     "M196_PUBLIC_APK_RC=0", "actual official DNS+HTTPS signed APK downloads"),
    ("apk info -e tree >/dev/null && apk info -e jq >/dev/null; "
     "printf 'M196_APK_DB_RC=%d\\n' \"$?\"\r",
     "M196_APK_DB_RC=0", "original guest installed-package database"),
    ("/usr/bin/tree --version >/dev/null && "
     "printf '{\"naseed\":true}\\n' | /usr/bin/jq -r .naseed | grep -qx true; "
     "printf 'M196_DYNAMIC_ELF_RC=%d\\n' \"$?\"\r",
     "M196_DYNAMIC_ELF_RC=0", "new original ELF and jq dynamic libraries"),
    ("rc-service crond start; printf 'M196_OPENRC_RC=%d\\n' \"$?\"\r",
     "M196_OPENRC_RC=0", "original Alpine service under live public network"),
    ("printf 'M196_PUBLIC_ALPINE_APK_2026\\n' > /root/m196-wan.txt && sync; "
     "printf 'M196_SYNC_RC=%d\\n' \"$?\"\r",
     "M196_SYNC_RC=0", "actual ext4 sync after verified public packages"),
    ("cat /root/m196-wan.txt\r", WITNESS,
     "persisted witness created only after both native programs execute"),
)


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: m196-alpine-uart.py RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M196_PUBLIC_SECONDS", "510"))
    M194.run(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]),
             Path(sys.argv[4]), seconds + 35, commands=COMMANDS, stage="M196",
             success_message="[M196 ALPINE PUBLIC APK PASS] original root PID1 "
                             "-> real DNS -> verified public HTTPS apk "
                             "-> tree+jq native ELF -> OpenRC -> ext4 sync")


if __name__ == "__main__":
    main()
