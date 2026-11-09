#!/usr/bin/env python3
"""Host-only M253 contract test: reject root-only/forged UID0 SSH proof.

This test does not claim QEMU kernel PASS or fabricate disk contents.
"""
import hashlib
import importlib.util
from pathlib import Path

def load(filename,alias):
    spec=importlib.util.spec_from_file_location(alias,Path(__file__).with_name(filename))
    assert spec is not None and spec.loader is not None
    obj=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj

uart=load("m253-alpine-multiuser-uart.py","m253_test_uart")
host=load("m253-host-ssh-two-epoch.py","m253_test_host")
disk=load("m253-verify-multiuser-disk.py","m253_test_disk")
first_markers=[x[1] for x in uart.FIRST_COMMANDS]
second_markers=[x[1] for x in uart.SECOND_COMMANDS]
for marker in (
    "M253_CREATE_REAL_USER_RC=0","M253_NONROOT_WRITE_RC=0",
    "M253_DAC_DENY_RC=0","M253_USER_KEY_RC=0",
    "M253_FIRST_USER_SSH_READY","M253_FIRST_HOST_NONROOT_RC=0"):
    assert marker in first_markers,marker
for marker in (
    "M253_SECOND_ACCOUNT_PERSIST_RC=0","M253_SECOND_NONROOT_DAC_RC=0",
    "M253_SECOND_USER_SSH_READY","M253_SECOND_HOST_NONROOT_RC=0"):
    assert marker in second_markers,marker
assert all(item[1] in first_markers for item in uart.m244.FIRST_COMMANDS)
assert all(item[1] in second_markers for item in uart.m244.SECOND_COMMANDS)
guest=" ".join(item[0] for item in uart.FIRST_COMMANDS+uart.SECOND_COMMANDS)
for token in ("su - m253user -c","test \"$(id -u)\" = 1101",
              "chpasswd","chmod 0700 /home/m253user",
              "chmod 0600 /home/m253user/m253-nonroot-data.txt",
              "if cat /root/m253-private/secret","kill -0 1","chown root:root",
              "M253_FIRST_HOST_NONROOT_RC","M253_SECOND_HOST_NONROOT_RC"):
    assert token in guest,token
assert guest.count("kill -0 1")>=2, "both epochs must probe nonroot PID1 signal permission"
assert "Operation not permitted" in guest, "must distinguish true EPERM from ESRCH/other errors"
assert any("kill -0 1" in command for command,*_ in uart.FIRST_COMMANDS)
assert any("kill -0 1" in command for command,*_ in uart.SECOND_COMMANDS)
assert host.TARGET=="m253user@127.0.0.1"
try:
    host.user_target(["ssh","root@127.0.0.1"])
    raise AssertionError("short argv should be refused")
except RuntimeError:
    pass
assert host.user_target(["ssh","-T","root@127.0.0.1","id"])[-2]==host.TARGET
try:
    host.user_target(["ssh","-T","other@127.0.0.1","id"])
    raise AssertionError("noncanonical root target should be refused")
except RuntimeError:
    pass

def record(phase):
    token="a"*48
    user={
        "phase":phase,"host":"127.0.0.1","host_port":12222,
        "guest_port":15446,"ssh_identity":host.TARGET,
        "effective_uid":1101,"effective_gid":1101,
        "public_key_only":True,"verified_root_dac_denial":True,
        "verified_nonroot_ext4_write":True,"session_count":1,
        "pty_allocated":True,"guest_ssh_ack_path":disk.ACK[phase][0],
        "guest_ssh_ack_sha256":hashlib.sha256(disk.ACK[phase][1]).hexdigest(),
        "client_pubkey_sha256":"b"*64,"host_key_fingerprint":"SHA256:"+"A"*43,
        "transport":"transport",
        "sessions":[{"nonce":token,"nonce_sha256":hashlib.sha256(token.encode()).hexdigest(),
                     "tty_allocated":True,"canonical_backspace":True,
                     "foreground_sigint":True,"winsize_33x101":True}],
    }
    root={
        "phase":phase,"client_pubkey_sha256":"b"*64,
        "host_key_fingerprint":"SHA256:"+"A"*43,"transport":"transport",
        "m253_nonroot":user,
    }
    return root

original=disk.m244.read_receipt
root=record("first")
try:
    disk.m244.read_receipt=lambda p,ph:root
    assert disk.receipt(Path("dummy"),"first")[1]["effective_uid"]==1101
    for field,bad in (
        ("effective_uid",0),("effective_gid",0),
        ("verified_root_dac_denial",False),
        ("verified_nonroot_ext4_write",False),
        ("ssh_identity","root@127.0.0.1"),
        ("public_key_only",False)):
        root["m253_nonroot"][field]=bad
        try:
            disk.receipt(Path("dummy"),"first")
            raise AssertionError("forged root-only M253 receipt accepted: "+field)
        except ValueError:
            pass
        root["m253_nonroot"][field]=record("first")["m253_nonroot"][field]
    root.pop("m253_nonroot")
    try:
        disk.receipt(Path("dummy"),"first")
        raise AssertionError("historical root-only M244 receipt falsely passed M253")
    except ValueError:
        pass
finally:
    disk.m244.read_receipt=original
print("[M253 STATIC CONTRACT PASS] inherits M244 signed gates; "
      "rejects root-only, UID0, forged DAC, untrusted SSH identity")
