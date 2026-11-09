#!/usr/bin/env python3
"""Official headless Alpine: real host SSH/PTTY on BOTH sides of one reset.

M243 supplies the original public APK, crond and durable ext4 workload.
M242 owns the single-QEMU-PID kernel reset, second OpenRC/getty/login,
natural poweroff and disk lock oracle. Host M244 independently authenticates
over genuine guest Dropbear, exercises a real PTY and writes one unique
guest-visible acknowledgment per boot; mere daemon readiness never passes.
"""

import importlib.util
import os
from pathlib import Path
import re
import sys


def sibling(filename: str, alias: str):
    spec = importlib.util.spec_from_file_location(alias, Path(__file__).with_name(filename))
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


m243 = sibling("m243-alpine-cli-uart.py", "naseed_m244_m243")
m242 = m243.m242

PUBKEY = "/root/m244-authorized-pubkey"
AUTHKEYS = "/root/.ssh/authorized_keys"
HOSTKEY = "/etc/dropbear/dropbear_ed25519_host_key"
HOSTKEY_WITNESS = "/root/m244-hostkey.sha256"
FIRST_RECEIPT = "/root/m244-first-host-pty.ok"
SECOND_RECEIPT = "/root/m244-second-host-pty.ok"
FIRST_VALUE = "M244_HOST_PTY_FIRST_OK_20261109"
SECOND_VALUE = "M244_HOST_PTY_SECOND_OK_20261109"
POLICY = ('DROPBEAR_OPTS="-s -g -j -k -m -p 0.0.0.0:15446 '
          '-r /etc/dropbear/dropbear_ed25519_host_key"')
KEY_POLICY = "no-port-forwarding,no-agent-forwarding,no-X11-forwarding"


FIRST_COMMANDS = m243.FIRST_COMMANDS + (
    ("test -x /root/m244-network-ready.sh && mkdir -p /etc/network && "
     "printf '%s\\n' 'auto eth0' 'iface eth0' "
     "'    pre-up /bin/sh /root/m244-network-ready.sh' > /etc/network/interfaces && "
     "/sbin/ifquery -i /etc/network/interfaces --list -a | "
     "/bin/busybox grep -qx eth0; "
     "printf 'M244_REAL_IFQUERY_RC=%d\\n' \"$?\"\r",
     "M244_REAL_IFQUERY_RC=0", "original Alpine ifupdown-ng reads real lwIP eth0"),
    ("rc-update add networking boot && "
     "rc-service networking start && rc-service networking status >/dev/null && "
     "test -L /etc/runlevels/boot/networking; "
     "printf 'M244_FIRST_NETWORKING_RC=%d\\n' \"$?\"\r",
     "M244_FIRST_NETWORKING_RC=0", "original OpenRC networking dependency started"),
    ("if apk info -e dropbear >/dev/null 2>&1 || "
     "apk info -e dropbear-openrc >/dev/null 2>&1; "
     "then echo M244_DROPBEAR_PREEXISTS; else echo M244_DROPBEAR_ABSENT; fi\r",
     "M244_DROPBEAR_ABSENT", "original signed Dropbear absent from base"),
    ("apk --no-cache add dropbear dropbear-openrc; "
     "printf 'M244_SIGNED_DROPBEAR_APK_RC=%d\\n' \"$?\"\r",
     "M244_SIGNED_DROPBEAR_APK_RC=0", "original public signed Dropbear packages"),
    ("test -x /usr/sbin/dropbear && test -x /usr/bin/dropbearkey && "
     "test -x /etc/init.d/dropbear && test -f /etc/conf.d/dropbear && "
     "test -f " + PUBKEY + " && test ! -L " + PUBKEY + "; "
     "printf 'M244_ORIGINAL_DROPBEAR_ELF_RC=%d\\n' \"$?\"\r",
     "M244_ORIGINAL_DROPBEAR_ELF_RC=0", "original Alpine Dropbear ELF and OpenRC service"),
    ("test ! -e " + AUTHKEYS + " && test ! -L " + AUTHKEYS + " && "
     "test ! -e " + HOSTKEY_WITNESS + " && test ! -L " + HOSTKEY_WITNESS + " && "
     "test ! -e " + FIRST_RECEIPT + " && test ! -L " + FIRST_RECEIPT + " && "
     "test ! -e " + SECOND_RECEIPT + " && test ! -L " + SECOND_RECEIPT + "; "
     "printf 'M244_UNSEEDED_SSH_STATE_RC=%d\\n' \"$?\"\r",
     "M244_UNSEEDED_SSH_STATE_RC=0", "no preseeded guest SSH state or host receipts"),
    ("read -r kind pub rest < " + PUBKEY + "; "
     "test \"$kind\" = ssh-ed25519 && test \"${#pub}\" -ge 68 && "
     "mkdir -p /root/.ssh /etc/dropbear && chmod 0700 /root/.ssh && "
     "printf '%s %s %s\\n' '" + KEY_POLICY + "' \"$kind\" \"$pub\" > "
     + AUTHKEYS + " && chmod 0600 " + AUTHKEYS + "; "
     "printf 'M244_SSH_AUTHORIZED_KEY_RC=%d\\n' \"$?\"\r",
     "M244_SSH_AUTHORIZED_KEY_RC=0", "host Ed25519 key staged for original root-only PTY"),
    ("! grep -qxF '" + POLICY + "' /etc/conf.d/dropbear && "
     "printf '%s\\n' '" + POLICY + "' >> /etc/conf.d/dropbear; "
     "printf 'M244_DROPBEAR_POLICY_RC=%d\\n' \"$?\"\r",
     "M244_DROPBEAR_POLICY_RC=0", "original Dropbear key-only nonforwarding policy"),
    ("rc-update add dropbear default && test -L /etc/runlevels/default/dropbear; "
     "printf 'M244_DROPBEAR_RC_UPDATE_RC=%d\\n' \"$?\"\r",
     "M244_DROPBEAR_RC_UPDATE_RC=0", "original OpenRC persistent Dropbear runlevel"),
    ("rc-service dropbear start && rc-service dropbear status >/dev/null && "
     "test -s /var/run/dropbear.pid && test -s " + HOSTKEY + "; "
     "printf 'M244_FIRST_DROPBEAR_PID_RC=%d\\n' \"$?\"\r",
     "M244_FIRST_DROPBEAR_PID_RC=0", "live original OpenRC Dropbear daemon"),
    ("/bin/busybox sha256sum " + HOSTKEY + " | /bin/busybox awk '{print $1}' > "
     + HOSTKEY_WITNESS + "; test -s " + HOSTKEY_WITNESS + "; "
     "printf 'M244_FIRST_HOSTKEY_RC=%d\\n' \"$?\"\r",
     "M244_FIRST_HOSTKEY_RC=0", "guest-generated Dropbear Ed25519 hostkey witness"),
    ("sync; printf 'M244_FIRST_SSH_SYNC_RC=%d\\n' \"$?\"\r",
     "M244_FIRST_SSH_SYNC_RC=0", "durable first-boot Dropbear identity"),
    ("printf 'M244_FIRST_HOST_SSH_READY\\n'\r",
     "M244_FIRST_HOST_SSH_READY", "first original Dropbear ready for real host PTY"),
    ("n=0; while [ ! -f " + FIRST_RECEIPT + " ] && [ \"$n\" -lt 220 ]; do "
     "n=$((n+1)); sleep 1; done; "
     "test -f " + FIRST_RECEIPT + " && "
     "grep -qxF '" + FIRST_VALUE + "' " + FIRST_RECEIPT + " && "
     "test ! -e " + SECOND_RECEIPT + "; "
     "printf 'M244_FIRST_HOST_PTY_RC=%d\\n' \"$?\"\r",
     "M244_FIRST_HOST_PTY_RC=0", "independent host authenticated PTY receipt before reset"),
    ("sync; printf 'M244_FIRST_PTY_SYNC_RC=%d\\n' \"$?\"\r",
     "M244_FIRST_PTY_SYNC_RC=0", "flush first host SSH receipt before original reboot"),
)


SECOND_COMMANDS = m243.SECOND_COMMANDS + (
    ("test -L /etc/runlevels/boot/networking && "
     "/sbin/ifquery -i /etc/network/interfaces --list -a | "
     "/bin/busybox grep -qx eth0 && "
     "rc-service networking status >/dev/null && "
     "grep -q '^nameserver 10[.]0[.]2[.]3$' /proc/net/naseed-resolv.conf; "
     "printf 'M244_SECOND_NETWORKING_AUTO_RC=%d\\n' \"$?\"\r",
     "M244_SECOND_NETWORKING_AUTO_RC=0", "original OpenRC networking auto-restored after reboot"),
    ("apk info -e dropbear >/dev/null && apk info -e dropbear-openrc >/dev/null && "
     "test -x /usr/sbin/dropbear && test -x /usr/bin/dropbearkey; "
     "printf 'M244_SECOND_DROPBEAR_APK_RC=%d\\n' \"$?\"\r",
     "M244_SECOND_DROPBEAR_APK_RC=0", "signed Dropbear APK database and native ELFs durable"),
    ("test -L /etc/runlevels/default/dropbear && "
     "rc-service dropbear status >/dev/null && test -s /var/run/dropbear.pid && "
     "kill -0 \"$(cat /var/run/dropbear.pid)\" && test -s " + HOSTKEY + "; "
     "printf 'M244_SECOND_DROPBEAR_AUTO_RC=%d\\n' \"$?\"\r",
     "M244_SECOND_DROPBEAR_AUTO_RC=0", "second OpenRC auto-started original Dropbear"),
    ("test -s " + AUTHKEYS + " && "
     "test \"$(/bin/busybox stat -c %a /root/.ssh)\" = 700 && "
     "test \"$(/bin/busybox stat -c %a " + AUTHKEYS + ")\" = 600 && "
     "grep -qxF '" + POLICY + "' /etc/conf.d/dropbear; "
     "printf 'M244_SECOND_SSH_POLICY_RC=%d\\n' \"$?\"\r",
     "M244_SECOND_SSH_POLICY_RC=0", "same secure guest SSH policy and key permissions"),
    ("test \"$(/bin/busybox sha256sum " + HOSTKEY + " | "
     "/bin/busybox awk '{print $1}')\" = \"$(cat " + HOSTKEY_WITNESS + ")\"; "
     "printf 'M244_SECOND_HOSTKEY_RC=%d\\n' \"$?\"\r",
     "M244_SECOND_HOSTKEY_RC=0", "same native Dropbear Ed25519 host key after reboot"),
    ("grep -qxF '" + FIRST_VALUE + "' " + FIRST_RECEIPT + " && "
     "test ! -e " + SECOND_RECEIPT + " && test ! -L " + SECOND_RECEIPT + "; "
     "printf 'M244_SECOND_PREHOST_RC=%d\\n' \"$?\"\r",
     "M244_SECOND_PREHOST_RC=0", "first phase SSH receipt survived; second was not preseeded"),
    ("printf 'M244_SECOND_HOST_SSH_READY\\n'\r",
     "M244_SECOND_HOST_SSH_READY", "reopened original Dropbear ready for real host PTY"),
    ("n=0; while [ ! -f " + SECOND_RECEIPT + " ] && [ \"$n\" -lt 220 ]; do "
     "n=$((n+1)); sleep 1; done; "
     "test -f " + SECOND_RECEIPT + " && "
     "grep -qxF '" + SECOND_VALUE + "' " + SECOND_RECEIPT + "; "
     "printf 'M244_SECOND_HOST_PTY_RC=%d\\n' \"$?\"\r",
     "M244_SECOND_HOST_PTY_RC=0", "independent host authenticated PTY receipt after reset"),
    ("sync; printf 'M244_SECOND_PTY_SYNC_RC=%d\\n' \"$?\"\r",
     "M244_SECOND_PTY_SYNC_RC=0", "flush second host SSH receipt before original poweroff"),
)

FIRST_MARKERS = tuple(item[1] for item in FIRST_COMMANDS)
SECOND_MARKERS = tuple(item[1] for item in SECOND_COMMANDS)


def verify_two_boots(text: str, *, reset: str = m242.RESET_MARKER) -> None:
    """Every guest phase marker must be original UART output, in epoch order."""
    # Retain every M243 physical reset + original public package / OpenRC
    # invariant in addition to the new SSH host receipt barriers.
    m243.verify_two_boots(text, reset=reset)
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
        if marker.startswith("M243_") or marker.startswith("M244_"):
            match = re.search(r"(?m)^" + re.escape(marker) + r"$", text[at:])
            index = at + match.start() if match else -1
        else:
            index = text.find(marker, at)
        if index < 0:
            raise RuntimeError("missing/out-of-order M244 original Alpine UART: " + marker)
        at = index + len(marker)


def run(runner: str, serial: Path, runner_log: Path, fifo: Path,
        deadline: int) -> None:
    m242.FIRST_COMMANDS = FIRST_COMMANDS
    m242.SECOND_COMMANDS = SECOND_COMMANDS
    m242.verify_two_boots = verify_two_boots
    m242.run(runner, serial, runner_log, fifo, deadline)
    print("[M244 ORIGINAL ALPINE TWO-EPOCH HOST SSH PASS] real original signed "
          "public Alpine APK + OpenRC/dropbear, independent host OpenSSH PTY "
          "receipt before and after SAME QEMU firmware reset, unchanged SSH "
          "identity, original second OpenRC auto-start, guest writes synced "
          "and natural original poweroff; physical host disk proof REQUIRED",
          flush=True)


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: m244-alpine-ssh-uart.py RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M244_DIAG_SECONDS", "1050"))
    run(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]), seconds + 35)


if __name__ == "__main__":
    main()
