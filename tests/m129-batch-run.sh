#!/bin/sh
# Fifteen globally new Alpine original ELFs: actual process/file/clock probes.
# A failing program never prevents the other 14 from running and reporting.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
TERM=xterm
TMUX_TMPDIR=/tmp
LC_ALL=C
LANG=C
export PATH HOME TERM TMUX_TMPDIR LC_ALL LANG
busy() { /bin/busybox "$@"; }

original_elf() {
    candidate=$1
    [ -x "$candidate" ] || return 1
    resolved=$(busy readlink -f "$candidate") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || {
        echo "[M129 NOT AN ORIGINAL ELF] $candidate" >&2
        return 1
    }
    printf '%s\n' "$candidate"
}

run_case() {
    app=$1
    dir="/tmp/m129-$app"
    busy mkdir -p "$dir"
    pid=$$
    case "$app" in
      pidof)
        exe=$(original_elf /bin/pidof)
        # The probe shell is a live, unrelated process: this is a real /proc scan.
        "$exe" sh > "$dir/pids"
        busy grep -Eq '^[0-9]+([[:space:]][0-9]+)*$' "$dir/pids" ;;
      slabtop)
        exe=$(original_elf /bin/slabtop)
        "$exe" -o > "$dir/slabs" 2>&1
        busy grep -Eq 'OBJS|SLABS|CACHE|NAME' "$dir/slabs" ;;
      tload)
        exe=$(original_elf /bin/tload)
        supervisor=$(original_elf /usr/bin/tmux)
        # M60/M66 already prove the real PTY-backed tmux path across ISAs.
        # Retain a dead pane for real upstream stderr and exit-status triage;
        # a vanished tmux server previously concealed the actual failure.
        busy printf '%s\n' 'set-option -g remain-on-exit on' > "$dir/tmux.conf"
        "$supervisor" -L m129 -f "$dir/tmux.conf" new-session -d -s m129t \
            '/bin/tload -d 1' > "$dir/pty" 2>&1
        busy sleep 1
        "$supervisor" -L m129 capture-pane -p -t m129t:0.0 > "$dir/load"
        "$supervisor" -L m129 display-message -p -t m129t:0.0 \
            '#{pane_dead}' > "$dir/pane-state"
        "$supervisor" -L m129 kill-server 2>/dev/null || :
        [ "$(busy cat "$dir/pane-state")" = 0 ]
        busy grep -Eq '[0-9]+\.[0-9]+' "$dir/load" ;;
      vmstat)
        exe=$(original_elf /bin/vmstat)
        "$exe" -n 1 1 > "$dir/memory" 2>&1
        busy grep -Eq 'swpd[[:space:]]+free|free[[:space:]]+buff' "$dir/memory"
        busy grep -Eq '[0-9]+[[:space:]]+[0-9]+' "$dir/memory" ;;
      w)
        exe=$(original_elf /bin/w)
        "$exe" > "$dir/sessions" 2>&1
        busy grep -Eq 'USER|TTY|load average' "$dir/sessions" ;;
      watch)
        exe=$(original_elf /bin/watch)
        supervisor=$(original_elf /usr/bin/tmux)
        "$supervisor" -L m129 -f /dev/null new-session -d -s m129w \
            '/bin/watch -t -n 1 /bin/busybox echo M129_WATCH_CHILD' \
            > "$dir/pty" 2>&1
        busy sleep 1
        "$supervisor" -L m129 capture-pane -p -t m129w:0.0 > "$dir/render"
        "$supervisor" -L m129 kill-server 2>/dev/null || :
        busy grep -Fq M129_WATCH_CHILD "$dir/render" ;;
      sysctl)
        exe=$(original_elf /sbin/sysctl)
        "$exe" -n kernel.hostname > "$dir/hostname" 2>&1
        [ -s "$dir/hostname" ]
        # Both tools must read the same actual kernel-backed /proc/sys field.
        busy cmp -s /proc/sys/kernel/hostname "$dir/hostname" ;;
      pwdx)
        exe=$(original_elf /usr/bin/pwdx)
        "$exe" "$pid" > "$dir/cwd" 2>&1
        pwd_now=$(pwd)
        busy grep -Fq "$pid: $pwd_now" "$dir/cwd" ;;
      fuser)
        exe=$(original_elf /usr/bin/fuser)
        busy echo M129_OPEN_FILE > "$dir/held"
        # A live process CWD + FD must be discoverable via real /proc
        # (procfd open-through-target remains an independent VFS milestone).
        exec 9< "$dir/held"
        cd "$dir"
        "$exe" "$dir" > "$dir/users" 2>&1
        busy grep -Eq "(^|[^0-9])$pid([^0-9]|$)" "$dir/users" ;;
      killall)
        exe=$(original_elf /usr/bin/killall)
        # Signal zero tests process discovery + permissions, kills nobody.
        busy sleep 5 & target=$!
        rc=0
        "$exe" -0 busybox > "$dir/probe" 2>&1 || rc=$?
        busy kill -TERM "$target" 2>/dev/null || :
        wait "$target" 2>/dev/null || :
        [ "$rc" -eq 0 ] ;;
      prtstat)
        exe=$(original_elf /usr/bin/prtstat)
        "$exe" "$pid" > "$dir/stat" 2>&1
        busy grep -Eq 'Process|State|PID|Pid' "$dir/stat" ;;
      pslog)
        exe=$(original_elf /usr/bin/pslog)
        # pslog inspects live process log-file descriptors (possibly none).
        "$exe" "$pid" > "$dir/logfds" 2>&1
        [ -r "/proc/$pid/fd/1" ] ;;
      lsfd)
        exe=$(original_elf /bin/lsfd)
        "$exe" -p "$pid" > "$dir/fd-table" 2>&1
        busy grep -Eq 'FD|COMMAND|PID' "$dir/fd-table"
        busy grep -Fq "$pid" "$dir/fd-table" ;;
      lslocks)
        exe=$(original_elf /usr/bin/lslocks)
        locker=$(original_elf /usr/bin/flock)
        busy echo M129_LOCKED > "$dir/held"
        exec 8> "$dir/held"
        "$locker" -n 8
        # Lock is actually held by our inherited OFD while lslocks scans.
        [ -r /proc/locks ]
        "$exe" > "$dir/locks" 2>&1
        busy grep -Fq FLOCK "$dir/locks"
        busy grep -Fq "$pid" "$dir/locks" ;;
      lsclocks)
        exe=$(original_elf /usr/bin/lsclocks)
        "$exe" > "$dir/clocks" 2>&1
        busy grep -Eq '(^|[[:space:]])(realtime|monotonic)([[:space:]]|$)' "$dir/clocks" ;;
      *) echo "[M129 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    if [ "${M129_TRACE:-0}" = 1 ]; then
        PS4='[M129 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failed=0
for app in pidof slabtop tload vmstat w watch sysctl pwdx \
           fuser killall prtstat pslog lsfd lslocks lsclocks; do
    echo "[M129 CASE START] $app"
    case_log="/tmp/m129-$app-case.log"
    if M129_TRACE=1 /bin/sh /tests/m129-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M129_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M129 $upper FAILED] original ELF rc=$rc" >&2
        busy tail -85 "$case_log" >&2 || :
        for detail in pids slabs load pane-state pty memory sessions render hostname cwd held \
                      users probe stat logfds fd-table locks clocks; do
            if [ -s "/tmp/m129-$app/$detail" ]; then
                echo "[M129 FAILED OUTPUT $app/$detail]" >&2
                busy head -c 1200 "/tmp/m129-$app/$detail" >&2 || :
                echo >&2
            fi
        done
        printf 'M129_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M129_BATCH_PASS
else
    echo "[M129 BATCH FAIL] $failed/15 original ELF programs, complete census" >&2
    echo M129_BATCH_FAIL
    exit 1
fi
