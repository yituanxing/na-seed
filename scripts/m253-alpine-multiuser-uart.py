#!/usr/bin/env python3
"""M253: genuine non-root original Alpine + SSH + ext4 DAC across 3 native ISAs.

Inherits M244 signed APK/OpenRC/host Dropbear + same-QEMU reset oracles.
This gate MUST fail on a root-only kernel; no UID-0 alias is accepted.
"""
import os
from pathlib import Path
import re
import shlex
import sys
from importlib.util import module_from_spec, spec_from_file_location

def sibling(filename, alias):
    spec = spec_from_file_location(alias, Path(__file__).with_name(filename))
    if spec is None or spec.loader is None:
        raise RuntimeError("M253 missing mandatory native Alpine oracle: " + filename)
    mod = module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod

m244 = sibling("m244-alpine-ssh-uart.py", "naseed_m253_m244")
m242 = m244.m242
U = "m253user"
UID = "1101"
HOME = "/home/m253user"
DATA = HOME + "/m253-nonroot-data.txt"
DATA_VALUE = "M253-NONROOT-REAL-DAC-PERSISTED"
ROOT_SECRET = "/root/m253-private/secret"
FIRST_ACK = HOME + "/m253-first-user-ssh.ok"
SECOND_ACK = HOME + "/m253-second-user-ssh.ok"
APP_WITNESS = HOME + "/m253-signed-apk-app.json"
FIRST_VALUE = "M253_NONROOT_SSH_FIRST_OK_20261109"
SECOND_VALUE = "M253_NONROOT_SSH_SECOND_OK_20261109"
SALT = "M253-PRIVATE-ROOT-SECRET-20261109"

def step(script, name, description):
    if not re.fullmatch(r"M253_[A-Z0-9_]+_RC=0", name):
        raise ValueError("bad M253 marker")
    label = name[:-1] + "%d"
    # Preserve nonzero status from the original busybox/Alpine command chain.
    return (script + "; /bin/busybox printf '" + label + "\\n' \"$?\"\r",
            name, description)

def as_user(shell):
    return "su - " + U + " -c " + shlex.quote(shell)

def deny_as_user(operation):
    return as_user("if " + operation + " >/dev/null 2>&1; then exit 97; else exit 0; fi")

def pid1_eperm():
    # Require actual EPERM, not ESRCH / unsupported kill / any other failure.
    return as_user("result=$(LC_ALL=C kill -0 1 2>&1); status=$?; "
                   "test \"$status\" -ne 0 && "
                   "printf \"%s\\n\" \"$result\" | grep -qi \"Operation not permitted\"")

def nonroot_apk_probe(check_app=False):
    # These are the ORIGINAL public-APK-installed musl ELF, not BusyBox applets.
    cmd = ("test -x /usr/bin/jq && test -x /usr/bin/tree && "
           "test \"$(printf '%s\\n' " + shlex.quote('{"uid":1101}') +
           " | /usr/bin/jq -r .uid)\" = 1101 && "
           "/usr/bin/tree " + HOME + " | /bin/busybox grep -q m253-nonroot-data.txt")
    if check_app:
        cmd += (" && /usr/bin/jq -e " +
                shlex.quote('.user == "m253user" and .uid == 1101 and .program == "jq"') +
                " " + APP_WITNESS + " >/dev/null")
    return as_user(cmd)

FIRST_COMMANDS = m244.FIRST_COMMANDS + (
    step("missing=0; for app in addgroup adduser chpasswd su id chmod chown stat; do "
         "/bin/busybox --list | /bin/busybox grep -qx \"$app\" || missing=1; "
         "done; test \"$missing\" = 0 && test ! -e " + HOME + " && "
         "! /bin/busybox grep -q '^m253user:' /etc/passwd",
         "M253_APPLETS_ORIGINAL_RC=0", "signed original Alpine user management applets"),
    step("addgroup -g " + UID + " " + U + " && "
         "adduser -D -u " + UID + " -G " + U + " -h " + HOME +
         " -s /bin/ash " + U + " && "
         "printf '%s\\n' 'm253user:M253-disposable-test-only-password' | chpasswd && "
         "chmod 0700 " + HOME + " && "
         "test \"$(id -u " + U + ")\" = " + UID + " && "
         "test \"$(id -g " + U + ")\" = " + UID + " && "
         "test \"$(/bin/busybox stat -c %u:%g " + HOME + ")\" = " + UID + ":" + UID,
         "M253_CREATE_REAL_USER_RC=0", "real uid/gid and non-locked Alpine account"),
    step("mkdir -p /root/m253-private && chmod 0700 /root/m253-private && "
         "printf '%s\\n' '" + SALT + "' > " + ROOT_SECRET + " && "
         "chmod 0600 " + ROOT_SECRET + " && "
         "test \"$(/bin/busybox stat -c %u:%g:%a " + ROOT_SECRET + ")\" = 0:0:600",
         "M253_ROOT_PRIVATE_RC=0", "root-only secret on physical ext4"),
    step(as_user("test \"$(id -u)\" = " + UID + " && "
                 "test \"$(id -g)\" = " + UID + " && "
                 "printf '%s\\n' '" + DATA_VALUE + "' > " + DATA + " && "
                 "chmod 0600 " + DATA + " && "
                 "test \"$(/bin/busybox stat -c %u:%g " + DATA + ")\" = " + UID + ":" + UID) +
         " && test \"$(/bin/busybox stat -c %u:%g " + DATA + ")\" = " + UID + ":" + UID,
         "M253_NONROOT_WRITE_RC=0", "unprivileged process creates persistent ext4 inode"),
    step(nonroot_apk_probe(),
         "M253_NONROOT_SIGNED_APK_RC=0",
         "original newly apk-installed musl ELF jq/tree work as UID1101"),
    step(deny_as_user("cat " + ROOT_SECRET) + " && " +
         pid1_eperm() + " && " +
         deny_as_user("touch /root/m253-forbidden") + " && " +
         deny_as_user("chmod 0777 " + ROOT_SECRET) + " && " +
         deny_as_user("chown root:root " + DATA) + " && " +
         "test ! -e /root/m253-forbidden && "
         "test \"$(/bin/busybox stat -c %u:%g " + DATA + ")\" = " + UID + ":" + UID,
         "M253_DAC_DENY_RC=0", "nonroot denied root secret/read/write/chmod/chown"),
    step("test ! -e " + HOME + "/.ssh/authorized_keys && "
         "mkdir -p " + HOME + "/.ssh && "
         "read -r kind pub rest < " + m244.PUBKEY + " && "
         "test \"$kind\" = ssh-ed25519 && test \"${#pub}\" -ge 68 && "
         "printf '%s %s %s\\n' '" + m244.KEY_POLICY + "' \"$kind\" \"$pub\" > " +
         HOME + "/.ssh/authorized_keys && "
         "chown -R " + UID + ":" + UID + " " + HOME + "/.ssh && "
         "chmod 0700 " + HOME + "/.ssh && "
         "chmod 0600 " + HOME + "/.ssh/authorized_keys && "
         "test \"$(/bin/busybox stat -c %u:%g:%a " + HOME + "/.ssh/authorized_keys)\" = "
         + UID + ":" + UID + ":600",
         "M253_USER_KEY_RC=0", "nonroot same host-only Ed25519 key secure ownership"),
    ("printf 'M253_FIRST_USER_SSH_READY\\n'\r",
     "M253_FIRST_USER_SSH_READY", "independent host authenticates real UID1101"),
    step("i=0; while [ ! -f " + FIRST_ACK + " ] && [ \"$i\" -lt 220 ]; do "
         "i=$((i+1)); sleep 1; done; "
         "test \"$(/bin/busybox stat -c %u:%g " + FIRST_ACK + ")\" = "
         + UID + ":" + UID + " && "
         "grep -qxF '" + FIRST_VALUE + "' " + FIRST_ACK + " && "
         "test ! -e " + SECOND_ACK,
         "M253_FIRST_HOST_NONROOT_RC=0", "guest-owned host-created SSH acknowledgment"),
    step("sync", "M253_FIRST_DURABLE_RC=0", "flush real nonroot data before reboot"),
)

SECOND_COMMANDS = m244.SECOND_COMMANDS + (
    step("test \"$(id -u " + U + ")\" = " + UID + " && "
         "test \"$(id -g " + U + ")\" = " + UID + " && "
         "test \"$(/bin/busybox stat -c %u:%g " + HOME + ")\" = " + UID + ":" + UID + " && "
         "grep -qxF '" + DATA_VALUE + "' " + DATA + " && "
         "grep -qxF '" + FIRST_VALUE + "' " + FIRST_ACK + " && "
         "test ! -e " + SECOND_ACK,
         "M253_SECOND_ACCOUNT_PERSIST_RC=0", "identity/data survive real firmware reboot"),
    step(as_user("test \"$(id -u)\" = " + UID + " && "
                 "test \"$(id -g)\" = " + UID + " && "
                 "test \"$(cat " + DATA + ")\" = '" + DATA_VALUE + "'") + " && " +
         deny_as_user("cat " + ROOT_SECRET) + " && " +
         pid1_eperm() + " && " +
         deny_as_user("touch /root/m253-forbidden-reboot") + " && "
         "test ! -e /root/m253-forbidden-reboot && "
         "test \"$(/bin/busybox stat -c %u:%g " + DATA + ")\" = " + UID + ":" + UID,
         "M253_SECOND_NONROOT_DAC_RC=0", "rebooted nonroot read succeeds, root DAC still denied"),
    step(nonroot_apk_probe(check_app=True) + " && " +
         "test \"$(/bin/busybox stat -c %u:%g:%a " + APP_WITNESS + ")\" = " + UID + ":" + UID + ":600",
         "M253_SECOND_NONROOT_APK_RC=0",
         "rebooted UID1101 executes signed musl jq/tree and persistent app JSON"),
    ("printf 'M253_SECOND_USER_SSH_READY\\n'\r",
     "M253_SECOND_USER_SSH_READY", "original Dropbear again accepts true nonroot SSH"),
    step("i=0; while [ ! -f " + SECOND_ACK + " ] && [ \"$i\" -lt 220 ]; do "
         "i=$((i+1)); sleep 1; done; "
         "test \"$(/bin/busybox stat -c %u:%g " + SECOND_ACK + ")\" = "
         + UID + ":" + UID + " && "
         "grep -qxF '" + SECOND_VALUE + "' " + SECOND_ACK,
         "M253_SECOND_HOST_NONROOT_RC=0", "second unique real nonroot SSH ACK"),
    step("sync", "M253_SECOND_DURABLE_RC=0", "sync second nonroot ACK before poweroff"),
)
FIRST_MARKERS = tuple(item[1] for item in FIRST_COMMANDS if item[1].startswith("M253_"))
SECOND_MARKERS = tuple(item[1] for item in SECOND_COMMANDS if item[1].startswith("M253_"))

def verify_two_boots(text, *, reset=m242.RESET_MARKER):
    m244.verify_two_boots(text, reset=reset)
    # M244 original markers remain mandatory, and all M253 assertions are
    # anchored to actual UART lines in the correct pre/post reset epoch.
    at = 0
    for marker in (*FIRST_MARKERS, reset, *SECOND_MARKERS):
        if marker.startswith("M253_"):
            match = re.search(r"(?m)^" + re.escape(marker) + r"$", text[at:])
            idx = at + match.start() if match else -1
        else:
            idx = text.find(marker, at)
        if idx < 0:
            raise RuntimeError("M253 missing/out-of-order native nonroot marker: " + marker)
        at = idx + len(marker)

def run(runner, serial, runner_log, fifo, deadline):
    m242.FIRST_COMMANDS = FIRST_COMMANDS
    m242.SECOND_COMMANDS = SECOND_COMMANDS
    m242.verify_two_boots = verify_two_boots
    m242.run(runner, serial, runner_log, fifo, deadline,
             uart_send=m244.send_uart_m244)
    print("[M253 UART NONROOT VALIDATED] native signed Alpine UID1101 "
          "nonroot DAC and both host acknowledgments; disk and wire "
          "physical independent proof REQUIRED", flush=True)

def main():
    if len(sys.argv) != 5:
        raise SystemExit("usage: m253-alpine-multiuser-uart.py RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M253_DIAG_SECONDS", "1380"))
    run(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]), seconds + 35)

if __name__ == "__main__":
    main()
