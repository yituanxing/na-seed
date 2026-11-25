#!/usr/bin/env python3
"""Drive physical QEMU serial for the early native TTY ABI workloads.

Inject only after guest readiness, require the actual architecture debug exit,
retain each workload's independent semantic assertions and failure markers.
"""
import os
import select
import subprocess
import sys
import time

PASS = b"[PASS] cmdline userspace exit 0"
EXIT = {"riscv64": b"[RV64] userspace debug-exit status=0",
        "arm64": b"[ARM64] userspace debug-exit status=0"}
# deadline (x86, other), polling interval, (readiness, bytes, byte delay),
# required markers, disallowed marker, final success/failure diagnostics.
PROFILES = {
    "m21": ((50, 80), .05, ((b"[M21] parent-progress", b"wake\r", .012),),
            (b"[M21] parent-progress", b"[M21] child-tty-read-ok", b"[M21] tty-scheduler-ok"),
            b"[FAIL]", "M21 TTY SCHED PASS", "physical UART woke parked child", "M21 REAL TTY SCHED FAIL"),
    "m22": ((50, 80), .05, ((b"[M22] parent-progress", b"poll\r", .012),),
            (b"[M22] parent-progress", b"[M22] child-tty-poll-ok", b"[M22] poll-events-ok", b"[M22] pipe-poll-ok"),
            b"[FAIL]", "M22 POLL WAKE PASS", "physical UART woke parked child", "M22 REAL POLL WAKE FAIL"),
    "m23": ((60, 95), .05, ((b"[M23] int-parent-ready", b"\003", 0),
                            (b"[M23] stop-parent-ready", b"\032", 0)),
            (b"[M23] int-parent-ready", b"[M23] sigint-group-ok", b"[M23] stop-parent-ready",
             b"[M23] sigtstp-stop-ok", b"[M23] tty-signals-ok"),
            b"[M23 FAIL]", "M23 TTY SIGNAL PASS", "physical ^C foreground group + ^Z stop/CONT", "M23 REAL TTY SIGNAL FAIL"),
    "m25": ((65, 95), .06, ((b"[M25] input-ready", b"bgwake\r", .012),),
            (b"[M25] sigttin-stop-ok", b"[M25] input-ready", b"[M25] background-read-ok",
             b"[M25] sigttou-stop-ok", b"[M25] background-write-ok", b"[M25] ignored-read-eio-ok",
             b"[M25] tty-background-jobs-ok"),
            b"[M25 FAIL]", "M25 TTY JOB PASS", "background SIGTTIN/SIGTTOU, SIGCONT and EIO", "M25 REAL TTY JOB FAIL"),
}


def run(profile, iso, port):
    durations, interval, actions, checks, forbidden, success, detail, failure = PROFILES[profile]
    launcher = (["sh", "scripts/run.sh", iso] if port == "amd64" else
                ["sh", "scripts/m18-port-qemu.sh", port, iso])
    p = subprocess.Popen(launcher, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, bufsize=0)
    log = bytearray()
    stage = 0
    deadline = time.monotonic() + durations[port != "amd64"]
    try:
        while time.monotonic() < deadline:
            ready, _, _ = select.select([p.stdout], [], [], interval)
            if ready:
                data = os.read(p.stdout.fileno(), 65536)
                if not data:
                    break
                log.extend(data)
            while stage < len(actions) and actions[stage][0] in log:
                _, payload, delay = actions[stage]
                for c in payload:
                    p.stdin.write(bytes([c]))
                    p.stdin.flush()
                    if delay:
                        time.sleep(delay)
                stage += 1
            # The common PASS can precede the actual hardware exit marker.
            if stage == len(actions) and PASS in log:
                if port == "amd64" and p.poll() is not None:
                    break
                if port != "amd64" and EXIT[port] in log:
                    break
            if p.poll() is not None:
                break
        if p.poll() is None:
            p.kill()
        p.wait(timeout=5)
        while True:
            data = os.read(p.stdout.fileno(), 65536)
            if not data:
                break
            log.extend(data)
    finally:
        p.stdin.close()
        p.stdout.close()
    out = bytes(log).replace(b"\r", b"")
    sys.stdout.buffer.write(out)
    sys.stdout.buffer.flush()
    exited = p.returncode == 1 if port == "amd64" else EXIT[port] in out
    sent = stage == len(actions)
    if not (sent and exited and PASS in out and all(k in out for k in checks)
            and b"[DEADLOCK]" not in out and forbidden not in out):
        if profile == "m23":
            sys.exit(f"[{failure}] ISA={port} stage={stage} exit={p.returncode}")
        if profile == "m25":
            sys.exit(f"[{failure}] arch={port} sent={sent} rc={p.returncode}")
        sys.exit(f"[{failure}] {port} input={sent} qemu_rc={p.returncode}")
    print(f"[{success}] {port} {detail}")


if __name__ == "__main__":
    profile = os.environ.get("NASEED_TTY_PROFILE", "")
    port = sys.argv[2] if len(sys.argv) == 3 else "amd64"
    if len(sys.argv) not in (2, 3) or profile not in PROFILES or port not in ("amd64", "riscv64", "arm64"):
        sys.exit("usage: NASEED_TTY_PROFILE=m21|m22|m23|m25 source-busybox-tty-serial.py ISO [amd64|riscv64|arm64]")
    run(profile, sys.argv[1], port)
