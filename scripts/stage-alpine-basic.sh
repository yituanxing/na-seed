#!/bin/sh
# Shared simple signed-overlay staging; preserve each historical proof marker.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
profile=$2
case "$profile" in
    m58-runtime) ;;
    m59-worktree) ;;
    m60-terminal) ;;
    m65-observe) ;;
    m66-monitor) ;;
    m67-control) ;;
    m69-inspect) ;;
    m70-vmfd) ;;
    m72-strace) ;;
    m73-strace) ;;
    m74-strace) ;;
    m75-gdb) ;;
    m79-workflow) ;;
    *) exit 2 ;;
esac
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" "$profile"
case "$profile" in
    m58-runtime) echo "[M58 HOST STAGE] $port signed original Bash/Tcl/OpenSSL, zero guest compilation" ;;
    m59-worktree) echo "[M59 HOST STAGE] $port original rsync/diffutils/patch/file; no guest compiler" ;;
    m60-terminal) echo "[M60 HOST STAGE] $port original signed less/tmux, no guest compiler" ;;
    m65-observe) echo "[M65 HOST STAGE] $port original signed procps-ng" ;;
    m66-monitor) echo "[M66 STAGE] native original signed top/htop/tmux $port" ;;
    m67-control) echo "[M67 STAGE] native original signed pgrep/pkill $port" ;;
    m69-inspect) echo "[M69 HOST STAGED] original pstree ps taskset prlimit $port" ;;
    m70-vmfd) echo "[M70 HOST STAGED] original pmap and lsof $port" ;;
    m72-strace) echo "[M72 HOST STAGED] original signed Alpine strace $port" ;;
    m73-strace) echo "[M73 HOST STAGED] original signed Alpine strace $port" ;;
    m74-strace) echo "[M74 HOST STAGED] original signed Alpine strace $port" ;;
    m75-gdb) echo "[M75 HOST STAGED] original signed Alpine GDB $port" ;;
    m79-workflow) echo "[M79 HOST STAGED] $port original Bash + Vim + Git + Python native ELF" ;;
esac
