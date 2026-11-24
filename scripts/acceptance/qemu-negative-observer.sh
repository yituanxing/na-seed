# Sourced by H12–H15 negative gates after their own input/ambient checks.
# Own and reap the runner and observer, retain actual serial bytes and rc.
# The caller still requires explicit rejection and forbids fallback markers.
qemu_pid=''
observer_pid=''
trap 'for child in "$observer_pid" "$qemu_pid"; do if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || :; fi; done' EXIT
rc=0
env "$negative_flag=1" \
    timeout --signal=TERM --kill-after=3s 75s \
    sh scripts/run.sh "$image" > "$serial_log" 2>&1 &
qemu_pid=$!
(
    while :; do
        if grep -Fq "$expected" "$serial_log"; then
            kill -TERM "$qemu_pid" 2>/dev/null || :
            exit 0
        fi
        if grep -Eq '^\[(FAIL|FAULT|EXEC REJECT|ELF REJECT)\]' "$serial_log"; then
            kill -TERM "$qemu_pid" 2>/dev/null || :
            exit 1
        fi
        sleep 0.2
    done
) &
observer_pid=$!
wait "$qemu_pid" || rc=$?
kill -TERM "$observer_pid" 2>/dev/null || :
wait "$observer_pid" 2>/dev/null || :
qemu_pid=''
observer_pid=''
tr -d '\r' < "$serial_log" > "$clean"
