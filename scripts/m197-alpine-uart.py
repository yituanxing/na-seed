#!/usr/bin/env python3
"""Original Alpine PID1, public multi-APK install and native app workflow."""

import importlib.util
import os
from pathlib import Path
import sys


def load_m196():
    path = Path(__file__).with_name("m196-alpine-uart.py")
    spec = importlib.util.spec_from_file_location("m197_m196_uart", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("M196 original Alpine UART driver missing")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


M194 = load_m196().M194
WITNESS = "M197_PUBLIC_ALPINE_SOFTWARE_2026"
PACKAGES = ("bash", "lua5.4", "sqlite", "git")
def build_commands(*, cache_index=False):
    # The standalone M197 acceptance keeps its original no-cache contract.
    # M204 retains the exact same signed public APKINDEX on ext4 for the
    # subsequent twelve real-program installations in this single boot.
    apk_add = "apk add" if cache_index else "apk --no-cache add"
    return (
    ("n=0; while ! grep -q '^nameserver 10.0.2.3' "
     "/proc/net/naseed-resolv.conf 2>/dev/null; do "
     "n=$((n+1)); if [ \"$n\" -ge 90 ]; then break; fi; sleep 1; done; "
     "grep -q '^nameserver 10.0.2.3' /proc/net/naseed-resolv.conf; "
     "printf 'M197_DHCP_DNS_READY_RC=%d\\n' \"$?\"\r",
     "M197_DHCP_DNS_READY_RC=0",
     "bounded wait for a real DHCP-published procfs DNS address"),
    ("test -L /etc/resolv.conf && "
     "test \"$(readlink /etc/resolv.conf)\" = /proc/net/naseed-resolv.conf && "
     "grep -q '^nameserver 10.0.2.3' /etc/resolv.conf && "
     "test -s /etc/ssl/cert.pem; "
     "printf 'M197_LIVE_DNS_TRUST_RC=%d\\n' \"$?\"\r",
     "M197_LIVE_DNS_TRUST_RC=0", "DHCP-owned live proc DNS and original CA"),
    ("if apk info -e bash >/dev/null 2>&1 || "
     "apk info -e lua5.4 >/dev/null 2>&1 || "
     "apk info -e sqlite >/dev/null 2>&1 || "
     "apk info -e git >/dev/null 2>&1; "
     "then echo M197_PACKAGES_PREEXISTING; "
     "else echo M197_PACKAGES_ABSENT; fi\r",
     "M197_PACKAGES_ABSENT", "all four official packages absent before WAN"),
    (f"{apk_add} bash lua5.4 sqlite git; "
     "printf 'M197_PUBLIC_APK_RC=%d\\n' \"$?\"\r",
     "M197_PUBLIC_APK_RC=0", "one genuine public TLS signed package transaction"),
    ("apk info -e bash >/dev/null && apk info -e lua5.4 >/dev/null && "
     "apk info -e sqlite >/dev/null && apk info -e git >/dev/null; "
     "printf 'M197_APK_DB_RC=%d\\n' \"$?\"\r",
     "M197_APK_DB_RC=0", "original Alpine installed DB records all four"),
    ("/bin/bash /root/m197-software-workload.sh --first; "
     "printf 'M197_WORKLOAD_RC=%d\\n' \"$?\"\r",
     "M197_WORKLOAD_RC=0", "Bash Lua SQLite Git live multi-program workload"),
    ("rc-service crond start; printf 'M197_OPENRC_RC=%d\\n' \"$?\"\r",
     "M197_OPENRC_RC=0", "real OpenRC service under public package session"),
    ("sync; printf 'M197_SYNC_RC=%d\\n' \"$?\"\r",
     "M197_SYNC_RC=0", "sync the real physical ext4 root"),
    ("cat /root/m197-wan.txt\r", WITNESS,
     "witness produced only after four programs committed persistent work"),
    )


COMMANDS = build_commands()


def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: m197-alpine-uart.py RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M197_PUBLIC_SECONDS", "840"))
    M194.run(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]),
             Path(sys.argv[4]), seconds + 35, commands=COMMANDS, stage="M197",
             success_message="[M197 ALPINE PUBLIC SOFTWARE PASS] real DHCP DNS "
                             "-> verified public HTTPS apk four packages "
                             "-> Bash Lua SQLite Git workflow -> ext4 sync")


if __name__ == "__main__":
    main()
