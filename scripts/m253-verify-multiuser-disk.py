#!/usr/bin/env python3
"""M253 independent signed source + real uid1101 SSH + physical ext4 DAC oracle.

CLI is byte-for-byte M244-compatible:
  ISA SIGNED_ROOT IMG FIRST.json SECOND.json QEMU.pcap
Do not accept serial-only SUCCESS or root-only M244 receipts as M253.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

def sibling(filename,alias):
    spec=importlib.util.spec_from_file_location(alias,Path(__file__).with_name(filename))
    if spec is None or spec.loader is None:
        raise RuntimeError("M253 missing inherited physical oracle "+filename)
    obj=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj

m244=sibling("m244-verify-ssh-disk.py","naseed_m253_m244_disk")
ACK={
    "first":("/home/m253user/m253-first-user-ssh.ok",b"M253_NONROOT_SSH_FIRST_OK_20261109\n"),
    "reopen":("/home/m253user/m253-second-user-ssh.ok",b"M253_NONROOT_SSH_SECOND_OK_20261109\n"),
}
USER="m253user"
UID=1101
DATA="/home/m253user/m253-nonroot-data.txt"
APP="/home/m253user/m253-signed-apk-app.json"
ROOT_SECRET="/root/m253-private/secret"

def receipt(path,phase):
    root=m244.read_receipt(path,phase)
    user=root.get("m253_nonroot")
    if not isinstance(user,dict):
        raise ValueError("M253 root-only historical M244 receipt cannot PASS")
    required={
       "phase":phase,"host":"127.0.0.1","host_port":12222,"guest_port":15446,
       "ssh_identity":"m253user@127.0.0.1",
       "effective_uid":1101,"effective_gid":1101,
       "public_key_only":True,"verified_root_dac_denial":True,
       "verified_nonroot_ext4_write":True,
       "verified_signed_apk_musl_nonroot":True,
       "session_count":1,"pty_allocated":True,
       "guest_ssh_ack_path":ACK[phase][0],
       "guest_ssh_ack_sha256":hashlib.sha256(ACK[phase][1]).hexdigest(),
    }
    for key,expected in required.items():
        if type(user.get(key)) is not type(expected) or user[key]!=expected:
            raise ValueError("M253 real UID1101 receipt invariant absent: "+key)
    if (user.get("client_pubkey_sha256")!=root["client_pubkey_sha256"] or
            user.get("host_key_fingerprint")!=root["host_key_fingerprint"] or
            user.get("transport")!=root["transport"]):
        raise ValueError("M253 uid1101 used foreign SSH transport/host identity")
    sessions=user.get("sessions")
    if not isinstance(sessions,list) or len(sessions)!=1 or not isinstance(sessions[0],dict):
        raise ValueError("M253 missing real nonroot PTY")
    for flag in ("tty_allocated","canonical_backspace",
                 "foreground_sigint","winsize_33x101"):
        if sessions[0].get(flag) is not True:
            raise ValueError("M253 missing nonroot PTY behavior "+flag)
    nonce=sessions[0].get("nonce")
    if (not isinstance(nonce,str) or re.fullmatch("[0-9a-f]{48}",nonce) is None or
            sessions[0].get("nonce_sha256")!=hashlib.sha256(nonce.encode()).hexdigest()):
        raise ValueError("M253 no independent PTY nonce challenge")
    return root,user

def inode_stat(partition,path):
    run=subprocess.run(["debugfs","-R","stat "+path,str(partition)],
                       capture_output=True,text=True,check=True)
    if "Inode:" not in run.stdout or "Type:" not in run.stdout:
        raise ValueError("M253 physical ext4 inode missing: "+path)
    return run.stdout

def assert_inode(partition,path,uid,gid,mode,kind):
    txt=inode_stat(partition,path)
    if not re.search(r"\bUser:\s*"+str(uid)+r"\b",txt):
        raise ValueError("M253 wrong REAL ext4 inode UID: "+path)
    if not re.search(r"\bGroup:\s*"+str(gid)+r"\b",txt):
        raise ValueError("M253 wrong REAL ext4 inode GID: "+path)
    if not re.search(r"\bMode:\s*0*"+str(mode)+r"\b",txt):
        raise ValueError("M253 wrong REAL ext4 inode mode: "+path)
    if "Type: "+kind not in txt:
        raise ValueError("M253 wrong ext4 inode type: "+path)

def assert_absent(partition,path):
    run=subprocess.run(["debugfs","-R","stat "+path,str(partition)],
                       capture_output=True,text=True,check=True)
    if "Inode:" in run.stdout or "File not found" not in (run.stdout+run.stderr):
        raise ValueError("M253 forbidden ext4 path exists or cannot prove absent: "+path)

def verify(port,signed_root,disk,first,second,pcap):
    # Keep ALL older M243/M244 signed APK, real TCP/SSH host and native ELF
    # media identity checks; M253 adds independently required nonzero UID.
    m244.verify(port,signed_root,disk,first,second,pcap)
    a,ua=receipt(first,"first")
    b,ub=receipt(second,"reopen")
    if (ua["sessions"][0]["nonce"]==ub["sessions"][0]["nonce"] or
            a["sessions"][0]["nonce"] in
            (ua["sessions"][0]["nonce"],ub["sessions"][0]["nonce"]) or
            b["sessions"][0]["nonce"] in
            (ua["sessions"][0]["nonce"],ub["sessions"][0]["nonce"])):
        raise ValueError("M253 host reused root/uid1101 nonce challenge")
    m191=sibling("m191-alpine-root-disk.py","naseed_m253_m191_disk")
    m230=sibling("m230-dropbear-disk-check.py","naseed_m253_m230_packets")
    root=signed_root.resolve(strict=True)
    physical=disk.resolve(strict=True)
    for path in ("/home/m253user","/etc/shadow",DATA,APP,*[p[0] for p in ACK.values()]):
        # The pristine upstream Alpine DOES normally contain /etc/shadow.
        if path=="/etc/shadow":
            continue
        original=m191.guest_path(root,path)
        if original.exists() or original.is_symlink():
            raise ValueError("M253 official signed source preseeded nonroot witness "+path)
    pristine_passwd=m191.guest_path(root,"/etc/passwd").read_text(errors="replace")
    if re.search(r"(?m)^m253user:",pristine_passwd):
        raise ValueError("M253 official Alpine already contained test account")
    flow=m230.physical_ssh_flow(pcap.read_bytes(),"same-QEMU-two-epoch")
    if flow["matching_guest_ssh_tcp_sessions"]<4:
        raise ValueError("M253 physical NIC trace lacks root + nonroot authenticated sessions")
    with tempfile.TemporaryDirectory(prefix="m253-original-alpine-ext4-") as tmp:
        part=Path(tmp)/"root.ext4"
        m191.extract_partition(physical,part)
        read=lambda path:m191.debugfs_cat(part,path)
        passwd=read("/etc/passwd").decode("utf-8")
        group=read("/etc/group").decode("utf-8")
        shadow=read("/etc/shadow").decode("utf-8")
        if re.search(r"(?m)^m253user:[^:]*:1101:1101:[^:]*:/home/m253user:/bin/ash$",passwd) is None:
            raise ValueError("M253 persisted original Alpine passwd uid/gid missing")
        if re.search(r"(?m)^m253user:[^:]*:1101:",group) is None:
            raise ValueError("M253 persisted original Alpine group missing")
        entry=re.search(r"(?m)^m253user:([^:]*):",shadow)
        if entry is None or not entry.group(1) or entry.group(1).startswith(("!","*")):
            raise ValueError("M253 original Alpine shadow lacks active nonroot account")
        if read(DATA)!=b"M253-NONROOT-REAL-DAC-PERSISTED\n":
            raise ValueError("M253 nonroot data not committed to real ext4")
        if read(APP)!=b'{"user":"m253user","uid":1101,"program":"jq"}\n':
            raise ValueError("M253 real SSH nonroot original APK jq output did not survive reboot")
        if read(ROOT_SECRET)!=b"M253-PRIVATE-ROOT-SECRET-20261109\n":
            raise ValueError("M253 root-only secret changed")
        assert_inode(part,"/home/m253user",UID,UID,"700","directory")
        assert_inode(part,"/home/m253user/.ssh",UID,UID,"700","directory")
        assert_inode(part,"/home/m253user/.ssh/authorized_keys",UID,UID,"600","regular")
        assert_inode(part,DATA,UID,UID,"600","regular")
        assert_inode(part,APP,UID,UID,"600","regular")
        assert_inode(part,"/root/m253-private",0,0,"700","directory")
        assert_inode(part,ROOT_SECRET,0,0,"600","regular")
        if read("/home/m253user/.ssh/authorized_keys")!=read("/root/.ssh/authorized_keys"):
            raise ValueError("M253 nonroot key was not original host-only staged Ed25519")
        for phase,(path,content) in ACK.items():
            if read(path)!=content:
                raise ValueError("M253 genuine nonroot SSH ACK absent on physical ext4 "+phase)
            assert_inode(part,path,UID,UID,"600","regular")
        for path in ("/root/m253-forbidden","/root/m253-forbidden-reboot",
                     "/root/m253-host-ssh-breach"):
            assert_absent(part,path)
    print("[M253 ORIGINAL ALPINE MULTIUSER+DAC+SSH+EXT4 PASS] "+port+
          " inherited signed native M244; true uid1101 su, original "
          "Dropbear nonroot host SSH PTY both epochs, denied root DAC "
          "operations, original signed jq/tree musl ELF as nonroot both "
          "epochs, physical ext4 persistent nonroot-owned inodes")

if __name__=="__main__":
    if len(sys.argv)!=7:
        raise SystemExit("usage: m253-verify-multiuser-disk.py ISA SIGNED_ROOT IMG FIRST.json SECOND.json QEMU.pcap")
    verify(sys.argv[1],*map(Path,sys.argv[2:]))
