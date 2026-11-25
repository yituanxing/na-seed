#!/bin/sh
# Build original C Linux-ABI workloads and run the external Linux oracle.
# PTY workloads require a real controlling terminal; no mocked device/syscalls.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 amd64|riscv64|arm64" >&2; exit 2; }
port=$1
case "$port" in
    amd64) cc=gcc; emu= ;;
    riscv64) cc=riscv64-linux-gnu-gcc; emu=qemu-riscv64 ;;
    arm64) cc=aarch64-linux-gnu-gcc; emu=qemu-aarch64 ;;
    *) exit 2 ;;
esac
dir="build/source-busybox/$port"
mkdir -p "$dir"
run_workload() {
    source=$1; binary=$2; mode=$3; marker=$4; shift 4
    out="$dir/$binary"
    "$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie "tests/$source.c" -o "$out"
    if readelf -l "$out" | grep -q INTERP; then
        echo "[$binary] test binary must be static" >&2; exit 1
    fi
    case "$mode" in
        pty|tty)
            command -v script >/dev/null
            cmd="$out${*:+ $*}"
            if [ -n "$emu" ]; then
                if [ "$mode" = tty ]; then cmd="env NASEED_QEMU_USER_TTY=1 $emu $cmd";
                else cmd="$emu $cmd"; fi
            fi
            output=$(script -q -e -c "$cmd" /dev/null | tr -d '\r')
            printf '%s\n' "$output" | grep -Fxq "$marker" ;;
        exact|signals|plain)
            if [ "$mode" = plain ]; then
                if [ -n "$emu" ]; then output=$("$emu" "$out" "$@"); else output=$("$out" "$@"); fi
            else
                if [ -n "$emu" ]; then output=$(LC_ALL=C "$emu" "$out" "$@"); else output=$(LC_ALL=C "$out" "$@"); fi
            fi
            if [ "$mode" = signals ]; then
                printf '%s\n' "$output" | grep -Fxq "$marker"
                printf '%s\n' "$output" | grep -Fxq '[M24] pending-mask-ok'
                printf '%s\n' "$output" | grep -Fxq '[M24] ignored-signal-ok'
            else
                [ "$output" = "$marker" ] || { echo "[$binary LINUX ORACLE FAIL] $port: $output" >&2; exit 1; }
            fi ;;
        *) exit 2 ;;
    esac
}
run_workload m11-vm-lifecycle m11-vm exact m11-vm-ok
echo "[M11 LINUX ORACLE PASS] $port standard static libc process/VM lifecycle"
run_workload m12-process-lifecycle m12-process exact m12-process-ok
echo "[M12 LINUX ORACLE PASS] $port original C process/wait/pipe/orphan lifecycle"
if [ "$port" = amd64 ]; then oracle=--linux-oracle; else oracle=--linux-fork-oracle; fi
run_workload m13-init m13-init exact m13-init-ok "$oracle"
echo "[M13 LINUX ORACLE PASS] $port original Linux-ABI init $oracle; real execve and adoption require system QEMU"
run_workload m14-groups m14-groups exact m14-groups-ok
echo "[M14 LINUX ORACLE PASS] $port process groups, sessions and waitpid selectors"
run_workload m15-kill m15-kill exact m15-kill-ok
echo "[M15 LINUX ORACLE PASS] $port SIGKILL/parked process/group/waitpid"
run_workload m16-stop m16-stop exact m16-stop-ok
echo "[M16 LINUX ORACLE PASS] $port process groups, STOP/CONT, wait event and PC preservation"
run_workload m17-tty m17-tty tty m17-tty-ok
if [ "$port" = amd64 ]; then
    "$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie tests/m17-input.c -o "$dir/m17-input"
fi
echo "[M17 LINUX PTY PASS] $port native tty/session/group oracle"
run_workload m18-ash-init m18-ash-init pty m18-linux-pty-ok --linux-pty-oracle
echo "[M18 LINUX PTY PASS] $port native init tty/glibc ELF"
run_workload m21-tty-sched m21-tty-sched pty m21-linux-pty-ok --linux-pty-oracle
echo "[M21 LINUX PTY PASS] $port native ELF"
run_workload m22-poll m22-poll pty m22-linux-pty-ok --linux-pty-oracle
echo "[M22 LINUX PTY PASS] $port standard Linux ELF"
run_workload m23-signals m23-signals pty m23-linux-pty-ok --linux-pty-oracle
echo "[M23 LINUX PTY PASS] $port native ABI"
run_workload m24-signals m24-signals signals m24-signals-ok --linux-oracle
echo "[M24 LINUX SIGNAL PASS] $port masked/ignored/default native signals"
run_workload m25-tty-jobs m25-tty-jobs pty m25-linux-pty-ok --linux-pty-oracle
echo "[M25 LINUX PTY PASS] $port native session+termios ABI"
run_workload m29-time m29-time plain m29-time-ok
echo "[M29 LINUX ORACLE PASS] $port monotonic/sleep/poll"
run_workload m30-signal-handlers m30-signal plain m30-signal-ok
echo "[M30 LINUX ORACLE PASS] $port sigaction/rt_sigreturn/masks/EINTR"
