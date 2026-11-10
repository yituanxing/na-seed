#!/usr/bin/env python3
"""M253 inherited M244 root SSH plus genuine nonroot PTY and UID1101 ACK.

The two M244 CLI receipt files contain original M244 root proof AND nested
m253_nonroot evidence. Nothing can pass on a kernel restricted to UID0.
"""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import re
import secrets
import subprocess
import time

def sibling(filename, alias):
    spec=importlib.util.spec_from_file_location(alias,Path(__file__).with_name(filename))
    if spec is None or spec.loader is None:
        raise RuntimeError("M253 missing inherited SSH oracle "+filename)
    mod=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod

m244=sibling("m244-host-ssh-two-epoch.py","naseed_m253_m244_host")
PTY=m244.PTY
BASE=m244.BASE
TARGET="m253user@127.0.0.1"
READY={"first":b"M253_FIRST_USER_SSH_READY","reopen":b"M253_SECOND_USER_SSH_READY"}
ACK={"first":("/home/m253user/m253-first-user-ssh.ok","M253_NONROOT_SSH_FIRST_OK_20261109"),
     "reopen":("/home/m253user/m253-second-user-ssh.ok","M253_NONROOT_SSH_SECOND_OK_20261109")}
APP="/home/m253user/m253-signed-apk-app.json"
APP_QUERY="'.user == \"m253user\" and .uid == 1101 and .program == \"jq\"'"

def user_target(argv):
    if len(argv)<3 or argv[-2]!="root@127.0.0.1":
        raise RuntimeError("M253 original ssh argv changed")
    argv[-2]=TARGET
    return argv

def user_pty(phase,args,nonce,deadline):
    old_argv,old_prompt=PTY.ssh_argv,PTY.SHELL_PROMPT_RE
    def user_argv(*a,**kw):
        return user_target(old_argv(*a,**kw))
    try:
        PTY.ssh_argv=user_argv
        PTY.SHELL_PROMPT_RE=re.compile(rb"(?:^|[:~/])[^\n]{0,55}\$ ?$")
        session=PTY.run_session(phase,args.host_port,args.client_key,
                                args.known_hosts,nonce,1,deadline)
    finally:
        PTY.ssh_argv,PTY.SHELL_PROMPT_RE=old_argv,old_prompt
    for field in ("tty_allocated","canonical_backspace","foreground_sigint",
                  "winsize_33x101"):
        if session.get(field) is not True:
            raise ValueError("M253 genuine nonroot PTY feature missing: "+field)
    return session

def ack_command(phase):
    path,value=ACK[phase]
    # Prove that a normal SSH user can read APK's signed package DB and run
    # dynamically linked, newly downloaded musl tree/jq across physical reboot.
    # M243's original root-owned first boot installed both; M244's physical
    # disk verifier authenticates their APK origin and unchanged package DB.
    app=("test ! -e "+APP+"; "
         "/usr/bin/jq -cn --arg user m253user --argjson uid 1101 "
         "'{user:$user,uid:$uid,program:\"jq\"}' > "+APP+".pending; "
         "chmod 0600 "+APP+".pending; "
         "/bin/busybox mv "+APP+".pending "+APP) if phase=="first" else (
         "test -s "+APP+"; /usr/bin/jq -e "+APP_QUERY+" "+APP+" >/dev/null")
    return ("set -e; umask 077; "
            "test \"$(id -u)\" = 1101; test \"$(id -g)\" = 1101; "
            "test \"$HOME\" = /home/m253user; "
            "test \"$(/bin/busybox stat -c %u:%g /home/m253user/m253-nonroot-data.txt)\" = 1101:1101; "
            "grep -qxF M253-NONROOT-REAL-DAC-PERSISTED /home/m253user/m253-nonroot-data.txt; "
            # Alpine's nonroot SSH PATH intentionally does not include /sbin.
            # APK metadata is readable without root; use its absolute path.
            "/sbin/apk info -e jq >/dev/null; /sbin/apk info -e tree >/dev/null; "
            "test -x /usr/bin/jq; test -x /usr/bin/tree; "
            "/usr/bin/tree /home/m253user | /bin/busybox grep -q m253-nonroot-data.txt; "
            +app+"; "
            "if cat /root/m253-private/secret >/dev/null 2>&1; then exit 90; fi; "
            "if touch /root/m253-host-ssh-breach >/dev/null 2>&1; then exit 91; fi; "
            "test ! -e "+path+"; "
            "printf '%s\\n' '"+value+"' > "+path+".pending; "
            "/bin/busybox mv "+path+".pending "+path+"; /bin/busybox cat "+path)

def user_epoch(phase,args,client_sha,first):
    if phase=="reopen" and (not isinstance(first,dict) or first.get("phase")!="first"):
        raise ValueError("M253 first UID1101 epoch must pass before reopen")
    BASE.wait_for_guest_ready(args.uart,READY[phase],args.ready_timeout)
    deadline=time.monotonic()+args.ssh_timeout
    session=user_pty(phase,args,secrets.token_hex(24),deadline)
    fingerprint=BASE.known_host_fingerprint(args.known_hosts)
    if first is not None and (
            first.get("host_key_fingerprint")!=fingerprint or
            first.get("client_pubkey_sha256")!=client_sha or
            first.get("host_port")!=args.host_port):
        raise ValueError("M253 original nonroot SSH identity changed on reboot")
    remaining=int(deadline-time.monotonic())
    if remaining<2:
        raise TimeoutError("M253 no budget to write nonroot host ACK")
    argv=user_target(BASE.ssh_command(phase,args.host_port,args.client_key,
                     args.known_hosts,secrets.token_hex(24),min(45,remaining)))
    argv[-1]=ack_command(phase)
    trial=subprocess.run(argv,capture_output=True,timeout=min(45,remaining))
    expected=(ACK[phase][1]+"\n").encode("ascii")
    if trial.returncode!=0 or trial.stdout!=expected:
        raise ValueError("M253 SSH command not actually UID1101, DAC failed or ACK absent: "
                         +phase+" exit="+str(trial.returncode)+
                         " stderr_tail="+repr(trial.stderr[-600:]))
    return {
      "phase":phase,"host":"127.0.0.1","host_port":args.host_port,
      "guest_port":BASE.GUEST_PORT,"transport":PTY.TRANSPORT,
      "ssh_identity":TARGET,"effective_uid":1101,"effective_gid":1101,
      "public_key_only":True,"verified_root_dac_denial":True,
      "verified_nonroot_ext4_write":True,
      "verified_signed_apk_musl_nonroot":True,
      "session_count":1,"sessions":[session],"pty_allocated":True,
      "client_pubkey_sha256":client_sha,"host_key_fingerprint":fingerprint,
      "guest_ssh_ack_path":ACK[phase][0],
      "guest_ssh_ack_sha256":hashlib.sha256(expected).hexdigest(),
      "timestamp_utc":time.strftime("%Y-%m-%dT%H:%M:%SZ",time.gmtime()),
    }

def run(args):
    client_sha=m244.check_inputs(args)
    root_first=m244.perform_epoch("first",args,client_sha=client_sha,first=None)
    nonroot_first=user_epoch("first",args,client_sha,None)
    root_first["m253_nonroot"]=nonroot_first
    m244.write_receipt(args.first_receipt,root_first)
    print("[M253 FIRST HOST PASS] root and uid1101 real SSH PTY",flush=True)
    root_second=m244.perform_epoch("reopen",args,client_sha=client_sha,first=root_first)
    nonroot_second=user_epoch("reopen",args,client_sha,nonroot_first)
    root_second["m253_nonroot"]=nonroot_second
    m244.write_receipt(args.second_receipt,root_second)
    print("[M253 SECOND HOST PASS] persistent host fingerprint and real uid1101 "
          "SSH PTY; independent ext4 physical oracle REQUIRED",flush=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("uart",type=Path)
    parser.add_argument("host_port",type=BASE.valid_port)
    parser.add_argument("first_receipt",type=Path)
    parser.add_argument("second_receipt",type=Path)
    parser.add_argument("--client-key",type=Path,required=True)
    parser.add_argument("--known-hosts",type=Path,required=True)
    parser.add_argument("--ready-timeout",type=int,default=700)
    parser.add_argument("--ssh-timeout",type=int,default=220)
    run(parser.parse_args())

if __name__=="__main__":
    try:
        main()
    except (OSError,ValueError,RuntimeError,TimeoutError,
            subprocess.SubprocessError) as exc:
        raise SystemExit("[M253 HOST SSH FAIL] "+str(exc)) from exc
