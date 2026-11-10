#!/usr/bin/env python3
"""Upstream Alpine public APK + OpenRC auto-start across ONE real QEMU reset.

M242 owns the established authenticated, two-epoch, same-QEMU-PID serial
state machine. M243 changes ONLY real Alpine shell workloads and its strict
ordered oracle. Neither firmware reset nor guest service startup is faked
by this script. No desktop devices or native test executables are needed.
"""

import importlib.util
import os
from pathlib import Path
import re
import sys


source = Path(__file__).with_name("m242-alpine-reboot-uart.py")
spec = importlib.util.spec_from_file_location("naseed_m243_m242", source)
assert spec is not None and spec.loader is not None
m242 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m242)

NONCE = "M243_ORIGINAL_APK_OPENRC_REBOOT_20261109"
FILE = "/root/m243-cli-witness.txt"
FIRST_COMMANDS = (
    ("n=0; while ! grep -q '^nameserver 10.0.2.3' "
     "/proc/net/naseed-resolv.conf 2>/dev/null; do "
     "n=$((n+1)); if [ \"$n\" -ge 90 ]; then break; fi; sleep 1; done; "
     "grep -q '^nameserver 10.0.2.3' /proc/net/naseed-resolv.conf; "
     "printf 'M243_DHCP_DNS_RC=%d\\n' \"$?\"\r",
     "M243_DHCP_DNS_RC=0", "live DHCP DNS, not a host-injected address"),
    ("test -L /etc/resolv.conf && "
     "test \"$(readlink /etc/resolv.conf)\" = /proc/net/naseed-resolv.conf && "
     "grep -q '^nameserver 10.0.2.3' /etc/resolv.conf && "
     "test -s /etc/ssl/cert.pem; "
     "printf 'M243_LIVE_DNS_TRUST_RC=%d\\n' \"$?\"\r",
     "M243_LIVE_DNS_TRUST_RC=0", "original Alpine trust + live resolver"),
    ("if apk info -e tree >/dev/null 2>&1 || apk info -e jq >/dev/null 2>&1; "
     "then printf 'M243_PACKAGES_PREEXIST\\n'; "
     "else printf 'M243_PACKAGES_ABSENT\\n'; fi\r",
     "M243_PACKAGES_ABSENT", "tree and jq absent from original signed baseline"),
    ("apk --no-cache add tree jq; printf 'M243_PUBLIC_APK_RC=%d\\n' \"$?\"\r",
     "M243_PUBLIC_APK_RC=0", "genuine public HTTPS APK signed package transaction"),
    ("apk info -e tree >/dev/null && apk info -e jq >/dev/null; "
     "printf 'M243_FIRST_APK_DB_RC=%d\\n' \"$?\"\r",
     "M243_FIRST_APK_DB_RC=0", "upstream installed package database"),
    ("test -x /usr/bin/tree && test -x /usr/bin/jq && "
     "/usr/bin/tree --version >/dev/null && "
     "printf '{\"naseed\":true}\\n' | /usr/bin/jq -r .naseed | grep -qx true; "
     "printf 'M243_FIRST_APK_ELF_RC=%d\\n' \"$?\"\r",
     "M243_FIRST_APK_ELF_RC=0", "original newly installed dynamic ELF programs"),
    ("test -x /etc/init.d/crond; printf 'M243_CROND_SCRIPT_RC=%d\\n' \"$?\"\r",
     "M243_CROND_SCRIPT_RC=0", "original Alpine crond OpenRC service script"),
    ("rc-update add crond default; rc=$?; "
     "test \"$rc\" -eq 0 && test -L /etc/runlevels/default/crond; "
     "printf 'M243_FIRST_RC_UPDATE_RC=%d\\n' \"$?\"\r",
     "M243_FIRST_RC_UPDATE_RC=0", "original rc-update persisted default runlevel"),
    ("rc-service crond start; printf 'M243_FIRST_CROND_START_RC=%d\\n' \"$?\"\r",
     "M243_FIRST_CROND_START_RC=0", "original OpenRC live service start"),
    ("rc-service crond status >/dev/null && pidof crond >/dev/null; "
     "printf 'M243_FIRST_CROND_PID_RC=%d\\n' \"$?\"\r",
     "M243_FIRST_CROND_PID_RC=0", "real running first-boot crond Task"),
    ("printf '" + NONCE + "\\n' > " + FILE + "; "
     "printf 'M243_FIRST_EXT4_WRITE_RC=%d\\n' \"$?\"\r",
     "M243_FIRST_EXT4_WRITE_RC=0", "first-boot persistent ext4 nonce"),
    ("sync; printf 'M243_FIRST_SYNC_RC=%d\\n' \"$?\"\r",
     "M243_FIRST_SYNC_RC=0", "first-boot physical ext4 sync"),
)

# There is deliberately NO rc-service start command on boot 2. Original
# OpenRC must automatically spawn crond from the first boot's guest-created
# /etc/runlevels/default/crond symlink; package ELF and apk DB must survive.
SECOND_COMMANDS = (
    ("apk info -e tree >/dev/null && apk info -e jq >/dev/null; "
     "printf 'M243_SECOND_APK_DB_RC=%d\\n' \"$?\"\r",
     "M243_SECOND_APK_DB_RC=0", "both original APK packages persisted across reset"),
    ("test -x /usr/bin/tree && test -x /usr/bin/jq && "
     "/usr/bin/tree --version >/dev/null && "
     "printf '{\"naseed\":true}\\n' | /usr/bin/jq -r .naseed | grep -qx true; "
     "printf 'M243_SECOND_APK_ELF_RC=%d\\n' \"$?\"\r",
     "M243_SECOND_APK_ELF_RC=0", "both newly installed dynamic ELF programs reopen"),
    ("test -L /etc/runlevels/default/crond && "
     "rc-service crond status >/dev/null && pidof crond >/dev/null; "
     "printf 'M243_SECOND_CROND_AUTO_RC=%d\\n' \"$?\"\r",
     "M243_SECOND_CROND_AUTO_RC=0", "second-boot OpenRC auto-started crond"),
    ("v=$(cat " + FILE + "); printf 'M243_SECOND_EXT4_NONCE=%s\\n' \"$v\"\r",
     "M243_SECOND_EXT4_NONCE=" + NONCE, "same-image ext4 nonce exact readback"),
)

FIRST_MARKERS = tuple(step[1] for step in FIRST_COMMANDS)
SECOND_MARKERS = tuple(step[1] for step in SECOND_COMMANDS)


def verify_two_boots(text: str, *, reset: str = m242.RESET_MARKER) -> None:
    """Forbid stale first-epoch UART, forged package writes and host restart."""
    if "[FAIL]" in text or "TIOCSCTTY: Operation not permitted" in text or (
        any(marker in text for marker in m242.m193.OPENRC_FATAL)
    ):
        raise RuntimeError("original Alpine native guest reported failure")
    milestones = (
        *m242.BOOT,
        *FIRST_MARKERS,
        "[EXEC] replaced process with lwext4 ELF: /sbin/reboot",
        *m242.FLUSH,
        "[ALPINE REBOOT] original BusyBox; durable ext4 unmount",
        reset,
        *m242.BOOT,
        *SECOND_MARKERS,
        "[EXEC] replaced process with lwext4 ELF: /sbin/poweroff",
        *m242.FLUSH,
        "[ALPINE POWER_OFF] original BusyBox; durable ext4 unmount",
        "[PASS] cmdline userspace exit 0",
    )
    at = 0
    for marker in milestones:
        if marker.startswith("M243_"):
            match = re.search(r"(?m)^" + re.escape(marker) + r"$", text[at:])
            index = at + match.start() if match else -1
        else:
            index = text.find(marker, at)
        if index < 0:
            raise RuntimeError("missing/out-of-order M243 UART marker: " + marker)
        at = index + len(marker)
    parts = text.split(reset, 1)
    if len(parts) != 2 or any(
        "[EXEC] replaced process with lwext4 ELF: /bin/sh" not in part and
        "[EXEC] replaced process with lwext4 ELF: /bin/ash" not in part
        for part in parts
    ):
        raise RuntimeError("original Alpine ash was not executed on BOTH boots")


def run(runner: str, serial: Path, runner_log: Path, fifo: Path,
        deadline: int) -> None:
    # m242.run already proves same QEMU PID, two independent password logins,
    # original reboot/physical reset/second boot, final poweroff, image lock.
    # Swap only its original ash command sets and strict final ordered oracle.
    m242.FIRST_COMMANDS = FIRST_COMMANDS
    m242.SECOND_COMMANDS = SECOND_COMMANDS
    m242.verify_two_boots = verify_two_boots
    m242.run(runner, serial, runner_log, fifo, deadline)
    print("[M243 ORIGINAL ALPINE APK OPENRC SAME QEMU PASS] original public "
          "signed apk add tree+jq -> live ELF and original rc-update/crond "
          "-> native platform reset in SAME QEMU PID -> second original "
          "OpenRC auto-crond, package DB+ELF+ext4 nonce persisted -> "
          "original poweroff and natural QEMU exit; independent host "
          "fsck/package/runlevel readback still REQUIRED", flush=True)


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: m243-alpine-cli-uart.py RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M243_DIAG_SECONDS", "650"))
    run(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]), seconds + 35)


if __name__ == "__main__":
    main()
