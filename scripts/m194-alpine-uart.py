#!/usr/bin/env python3
"""Exercise original Alpine APK, OpenRC and ext4 through a real QEMU UART.

The M193 password-authenticated shell is the prerequisite, not a replacement.
Every M194 step waits for a new *guest output line*, never a command echo,
host-inferred return status, timer expiration or synthesized shell response.
"""

import fcntl
import importlib.util
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time


_m193_spec = importlib.util.spec_from_file_location(
    "naseed_m193_uart", Path(__file__).with_name("m193-alpine-uart.py"))
assert _m193_spec is not None and _m193_spec.loader is not None
m193 = importlib.util.module_from_spec(_m193_spec)
_m193_spec.loader.exec_module(m193)


# Commands are sent to upstream BusyBox ash after upstream getty/login has
# authenticated a real user.  The status marker is deliberately different
# from the text of the echoed shell command: output_line() requires the
# expected value on an entire line.  No guest test script or native ELF is
# added to the official distribution rootfs.
COMMANDS = (
    ("apk --version; printf 'M194_APK_ELF_RC=%d\\n' \"$?\"\r",
     "M194_APK_ELF_RC=0", "original apk executable"),
    ("apk info -e alpine-base && apk info -e openrc && apk info -e busybox-openrc; "
     "printf 'M194_APK_DB_RC=%d\\n' \"$?\"\r",
     "M194_APK_DB_RC=0", "signed distro package database"),
    ("rc-status; printf 'M194_RC_STATUS_RC=%d\\n' \"$?\"\r",
     "M194_RC_STATUS_RC=0", "original OpenRC service status"),
    ("test -x /etc/init.d/crond; printf 'M194_CROND_SCRIPT_RC=%d\\n' \"$?\"\r",
     "M194_CROND_SCRIPT_RC=0", "original crond OpenRC script"),
    ("rc-service crond start; printf 'M194_CROND_START_RC=%d\\n' \"$?\"\r",
     "M194_CROND_START_RC=0", "real OpenRC crond start"),
    ("rc-service crond status; printf 'M194_CROND_STATUS_RC=%d\\n' \"$?\"\r",
     "M194_CROND_STATUS_RC=0", "real OpenRC crond running status"),
    ("p=\"$(pidof crond)\"; test -n \"$p\" && "
     "printf 'M194_CROND_PID=%s\\n' \"$p\"\r",
     re.compile(r"(?:^|\n)M194_CROND_PID=([0-9]+(?: [0-9]+)*)(?:\n|$)"),
     "genuine crond process PID"),
    ("rc-service crond stop; printf 'M194_CROND_STOP_RC=%d\\n' \"$?\"\r",
     "M194_CROND_STOP_RC=0", "real OpenRC crond stop"),
    ("rc-service crond status >/dev/null 2>&1; rc=$?; "
     "[ \"$rc\" -ne 0 ] && printf 'M194_CROND_STOPPED\\n'\r",
     "M194_CROND_STOPPED", "crond stopped status"),
    ("pidof crond >/dev/null; rc=$?; "
     "[ \"$rc\" -ne 0 ] && printf 'M194_CROND_NO_PID\\n'\r",
     "M194_CROND_NO_PID", "crond process absent"),
    ("printf 'M194_DURABLE_REAL_EXT4_2026\\n' > /root/m194-persistent.txt; "
     "printf 'M194_EXT4_WRITE_RC=%d\\n' \"$?\"\r",
     "M194_EXT4_WRITE_RC=0", "original ash writable ext4 file"),
    ("sync; printf 'M194_SYNC_RC=%d\\n' \"$?\"\r",
     "M194_SYNC_RC=0", "guest sync completed"),
    ("cat /root/m194-persistent.txt\r",
     "M194_DURABLE_REAL_EXT4_2026", "guest ext4 readback of written bytes"),
)


def step_match(segment: str, expected: str | re.Pattern[str]) -> re.Match[str] | None:
    if isinstance(expected, str):
        return re.search(r"(?:^|\n)" + re.escape(expected) + r"(?:\n|$)", segment)
    return expected.search(segment)


def step_observed(segment: str, expected: str | re.Pattern[str]) -> bool:
    return step_match(segment, expected) is not None


# After a command's independent result marker, ash can print its next prompt
# and the kernel can immediately append an asynchronous printk on the SAME
# UART line.  Checking only the final line (m193.shell_prompt) loses that
# already-real prompt and deadlocks the next command until the QEMU timeout.
# Anchor the prompt at the start of a fresh UART line: neither a shell command
# echo ("naseed:~# apk ...") nor a printk mentioning "naseed:~#" is proof.
_UART_CSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
_ASH_PROMPT = re.compile(
    r"^(?:[A-Za-z0-9_.-]+:[~/][^\s#]{0,45}"
    r"|[A-Za-z0-9_.-]+@[A-Za-z0-9_.-]+:[~/][^\s#]{0,45}"
    r"|[~/][^\s#]{0,45})# ?"
)
_ASYNC_PRINTK = re.compile(
    r"^(?:\[(?:LWIP|VIRTIO|NET|IRQ|PROC|THREAD|TIMER|BLOCK|CLOCK|"
    r"SCHED|EXEC|DYN|UART|TTY)(?:[- ][A-Z0-9 _-]{0,70})?\]"
    r"|\[M177 VIRTIO GPU 2D (?:STATS|PASS)\]"
    r"|\[MISS\] syscall nr=[0-9]+\r?$)"
)


def shell_prompt_after_step(segment: str) -> bool:
    """Find a completed ash prompt *after* a verified command boundary.

    An optional VT100 DSR query (ESC[6n) and a kernel printk can follow a
    prompt without a newline.  Ignore those terminal sequences, but never
    accept a prompt merely embedded in an echoed command or kernel log.
    """
    for raw_line in segment.split("\n"):
        line = _UART_CSI.sub("", raw_line)
        prompt = _ASH_PROMPT.match(line)
        if prompt is None:
            continue
        suffix = line[prompt.end():]
        if not suffix or _ASYNC_PRINTK.match(suffix):
            return True
    return False


def disk_lock_released(image: str) -> bool:
    """Probe QEMU's advisory byte-range lock without changing the ext4 disk.

    QEMU's raw image writer takes an exclusive POSIX/OFD byte-range lock. A
    second QEMU must not start until a conflicting nonblocking lock succeeds;
    waiting for the *runner shell* alone does not prove its QEMU child exited.
    """
    fd = os.open(image, os.O_RDWR | os.O_CLOEXEC)
    try:
        try:
            fcntl.lockf(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return False
        return True
    finally:
        # Closing immediately releases this temporary probe lock. Never hold
        # it across the real second QEMU boot.
        os.close(fd)


def image_qemu_pids(image: str) -> list[int]:
    """Find only native QEMU processes with this exact ext4 image open.

    GNU ``timeout`` normally creates a separate child process group, so
    killpg(runner.pid) does not necessarily reach its ``qemu-system-*``
    process. Inspect Linux /proc FDs rather than guessing child PGIDs or
    killing every QEMU on the CI host. The dev/inode comparison survives
    relative paths and renamed open descriptors.
    """
    disk = os.stat(image)
    owners = []
    with os.scandir("/proc") as tasks:
        for task in tasks:
            if not task.name.isdecimal():
                continue
            pid = int(task.name)
            try:
                with open(f"/proc/{pid}/cmdline", "rb") as command_file:
                    cmdline = command_file.read(4096).split(b"\0", 1)[0]
                if not os.path.basename(os.fsdecode(cmdline)).startswith("qemu-system-"):
                    continue
                with os.scandir(f"/proc/{pid}/fd") as descriptors:
                    for descriptor in descriptors:
                        try:
                            opened = os.stat(descriptor.path)
                        except (FileNotFoundError, PermissionError):
                            continue
                        if (opened.st_dev, opened.st_ino) == (disk.st_dev, disk.st_ino):
                            owners.append(pid)
                            break
            except (FileNotFoundError, ProcessLookupError, PermissionError):
                continue  # Process exited, or another user's inaccessible VM.
    return owners


def runner_session_pids(sid: int) -> list[int]:
    """Find a runner's live descendants across GNU timeout process groups.

    Popen(start_new_session=True) gives our runner a private Linux session.
    GNU timeout's new process group does not move its QEMU child out of that
    session. Session membership is therefore a strict owner check, unlike a
    broad ``pkill qemu`` or an unverified PID obtained from a shell pipeline.
    """
    members = []
    with os.scandir("/proc") as tasks:
        for task in tasks:
            if not task.name.isdecimal():
                continue
            pid = int(task.name)
            try:
                if os.getsid(pid) == sid:
                    members.append(pid)
            except (ProcessLookupError, PermissionError):
                continue
    return members


def stop_runner(proc: subprocess.Popen[bytes]) -> None:
    """Stop the entire native-QEMU runner group and await image-lock release.

    ``sh runner`` starts ``timeout -> qemu``. GNU timeout may create its own
    process group, and the shell may exit before QEMU releases its open disk;
    neither shell wait() nor killpg(shell) is sufficient for this gate.
    """
    image = os.environ.get("NAS_VIRTIO_BLK_IMAGE")
    group = proc.pid

    def signal_group(sig: signal.Signals) -> None:
        try:
            os.killpg(group, sig)
        except ProcessLookupError:
            pass

    def signal_session(sig: signal.Signals) -> None:
        for pid in runner_session_pids(group):
            try:
                # A second membership check avoids signaling a PID which
                # exited and was recycled between scanning and delivery.
                if os.getsid(pid) == group:
                    os.kill(pid, sig)
            except (ProcessLookupError, PermissionError):
                pass

    def signal_disk_qemus(sig: signal.Signals) -> None:
        if image:
            for pid in image_qemu_pids(image):
                try:
                    os.kill(pid, sig)
                except ProcessLookupError:
                    pass

    def await_release(seconds: float) -> bool:
        until = time.monotonic() + seconds
        while True:
            shell_done = proc.poll() is not None
            disk_done = not image or disk_lock_released(image)
            if shell_done and disk_done:
                return True
            if time.monotonic() >= until:
                return False
            time.sleep(0.10)

    # The session catches GNU timeout's separately grouped QEMU; exact-image
    # file descriptors catch a VM that detached even from its parent session.
    signal_group(signal.SIGTERM)
    signal_session(signal.SIGTERM)
    signal_disk_qemus(signal.SIGTERM)
    if not await_release(8.0):
        signal_group(signal.SIGKILL)
        signal_session(signal.SIGKILL)
        signal_disk_qemus(signal.SIGKILL)
        if not await_release(8.0):
            remaining = image_qemu_pids(image) if image else []
            raise RuntimeError(
                f"first native QEMU did not release its raw ext4 disk lock; "
                f"exact-image QEMU PIDs={remaining}")
    proc.wait()


def run(runner: str, log_name: Path, runner_log: Path, fifo: Path, deadline: int,
        *, commands=COMMANDS, stage: str = "M194",
        success_message: str | None = None,
        failure_watch: Path | None = None,
        before_command_barriers: dict[int, Path] | None = None) -> None:
    if not commands:
        raise ValueError("Alpine UART acceptance needs at least one real guest command")
    fifo.parent.mkdir(parents=True, exist_ok=True)
    if fifo.exists():
        raise RuntimeError("refusing to reuse an existing serial FIFO: " + str(fifo))
    if log_name.exists():
        log_name.unlink()  # Never accept stale UART output from an older run.
    os.mkfifo(fifo, 0o600)
    hold = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
    runner_log.parent.mkdir(parents=True, exist_ok=True)
    proc = None
    observed = bytearray()
    state = "login"
    step = 0
    step_start = 0
    step_end = 0
    shell_at = 0.0
    dsr_replies = 0
    started = time.monotonic()
    try:
        with runner_log.open("wb") as runner_out:
            proc = subprocess.Popen(
                ["sh", runner], stdout=runner_out,
                stderr=subprocess.STDOUT, start_new_session=True)
            cursor = 0
            while time.monotonic() - started < deadline:
                # Optional *independent* physical-QMP oracle. A failed video
                # or hardware input observation should stop its Alpine UART
                # runner immediately, not conceal the cause for 20 minutes.
                if failure_watch is not None and failure_watch.is_file():
                    raise RuntimeError("independent physical QMP rejected GUI: "
                                       + failure_watch.read_text()[-2000:])
                if log_name.is_file():
                    with log_name.open("rb") as stream:
                        stream.seek(cursor)
                        chunk = stream.read()
                        cursor += len(chunk)
                    observed.extend(chunk)
                text = observed.decode("utf-8", errors="replace").replace("\r", "")
                if ("[FAIL]" in text or "TIOCSCTTY: Operation not permitted" in text
                        or any(marker in text for marker in m193.OPENRC_FATAL)):
                    raise RuntimeError("original Alpine reported a boot or tty failure")

                # Genuine upstream BusyBox ash waits for a VT100 cursor
                # position response before displaying its next prompt.
                while dsr_replies < text.count("\x1b[6n"):
                    m193.sent(hold, "\x1b[1;1R")
                    dsr_replies += 1

                if state == "login" and "naseed login:" in text:
                    print(f"[{stage} UART] original getty; submitting root", flush=True)
                    m193.sent(hold, "root\r")
                    state = "password"
                elif state == "password" and "Password:" in text:
                    print(f"[{stage} UART] original login requested password", flush=True)
                    m193.sent(hold, m193.PASSWORD + "\r")
                    state = "shell"
                elif state == "shell" and (
                    "[EXEC] replaced process with lwext4 ELF: /bin/sh" in text or
                    "[EXEC] replaced process with lwext4 ELF: /bin/ash" in text or
                    m193.shell_prompt(text)
                ):
                    if shell_at == 0.0:
                        shell_at = time.monotonic()
                    if time.monotonic() - shell_at >= 0.6:
                        print(f"[{stage} UART] authenticated original ash; probing services", flush=True)
                        step_start = len(text)
                        m193.sent(hold, commands[0][0])
                        state = "commands"
                elif state == "commands":
                    _command, expected, description = commands[step]
                    segment = text[step_start:]
                    found = step_match(segment, expected)
                    if found is not None:
                        print(f"[{stage} UART] verified {description}", flush=True)
                        step_end = step_start + found.end()
                        step += 1
                        if step == len(commands):
                            required = m193.PROOF + (
                                "[EXEC] replaced process with lwext4 ELF: /bin/login",
                            )
                            missing = [marker for marker in required if marker not in text]
                            if missing:
                                raise RuntimeError("missing physical QEMU login proof: "
                                                   + ", ".join(missing))
                            print(success_message or
                                  "[M194 ALPINE SERVICES PASS] original ext4 Alpine PID1 "
                                  "-> OpenRC -> password login -> genuine apk database "
                                  "-> rc-service crond start/status/pid/stop "
                                  "-> guest ext4 write/sync/readback; "
                                  "timeout was NOT a PASS", flush=True)
                            return
                        # Let upstream ash complete its read/execute/print
                        # cycle.  Its VT100 prompt may arrive later than the
                        # printf marker, especially around OpenRC daemon
                        # startup.  Queueing the next command too early can
                        # interleave with a cursor-position report.
                        state = "await_prompt"
                    elif shell_prompt_after_step(segment):
                        # A new REAL ash prompt means this command already
                        # returned.  Stop immediately when apk/rc-service or
                        # the daemon check failed; do not wait out the entire
                        # QEMU budget and conceal the original exit status.
                        observed_rc = re.findall(r"M194_[A-Z_]+_RC=[0-9]+", segment)
                        raise RuntimeError(
                            f"original Alpine command {step + 1} ({description}) "
                            f"completed without its required result; guest_rc={observed_rc[-1:]}")
                elif state == "await_prompt" and shell_prompt_after_step(text[step_end:]):
                    # A graphical controller may need to finish *independent*
                    # physical QMP keyboard/display checks before the next
                    # command stops an OpenRC-managed desktop. The file is
                    # only a host-side synchronization gate; success is still
                    # determined from genuine guest UART/QMP evidence.
                    barrier = (before_command_barriers or {}).get(step)
                    if barrier is None or barrier.is_file():
                        step_start = len(text)
                        m193.sent(hold, commands[step][0])
                        state = "commands"

                if proc.poll() is not None:
                    raise RuntimeError(f"native QEMU runner exited before proof: {proc.returncode}")
                time.sleep(0.10)
            raise RuntimeError(f"{stage} original Alpine proof timed out: state={state} step={step}")
    except Exception:
        print(f"[{stage} ALPINE SERVICES FAIL] raw QEMU UART tail:", file=sys.stderr)
        print(observed.decode("utf-8", errors="replace")[-13000:], file=sys.stderr)
        raise
    finally:
        try:
            if proc is not None:
                stop_runner(proc)
        finally:
            # Preserve the original physical UART evidence and release FIFO
            # handles even if an uncooperative QEMU fails the lock-release gate.
            os.close(hold)
            fifo.unlink()
            if observed:
                clean_name = log_name.with_name(
                    log_name.name.removesuffix(".serial.log") + ".clean.log")
                clean_name.write_bytes(bytes(observed).replace(b"\r", b""))


def main() -> None:
    if len(sys.argv) != 5:
        raise SystemExit("usage: m194-alpine-uart.py RUNNER SERIAL_LOG RUNNER_LOG FIFO")
    seconds = int(os.environ.get("NAS_M194_DIAG_SECONDS", "150"))
    run(sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]), seconds + 35)


if __name__ == "__main__":
    main()
