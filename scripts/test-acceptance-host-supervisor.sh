#!/bin/sh
# POSIX-sh negative and live-child tests for the acceptance EXIT owner.
set -eu
module=scripts/acceptance/host-supervisor.sh
test -r "$module"
test_dir=$(mktemp -d) || exit 1
test_started=$(date +%s)
trap 'rm -rf -- "$test_dir"' 0
fail() { printf '[ACCEPTANCE SUPERVISOR FAIL] %s\n' "$1" >&2; exit 1; }

# Bad and ambiguous PIDs must never reach kill, even in an EXIT trap.
sh -c '
    set -eu
    . "$1"
    nas_acceptance_host_supervisor_install
    nas_acceptance_host_supervisor_install
    for bad in "" 0 00 1 01 -1 -2 abc "2 3" "3x" 999999999999999999999999999999; do
        if nas_acceptance_host_supervisor_register "$bad"; then exit 10; fi
        if nas_acceptance_host_supervisor_unregister "$bad"; then exit 11; fi
    done
    if nas_acceptance_host_supervisor_register "$$"; then exit 12; fi
    if nas_acceptance_host_supervisor_register; then exit 13; fi
    if nas_acceptance_host_supervisor_register 2 3; then exit 14; fi
    test -z "$nas_acceptance_host_supervisor_pids"
' sh "$module" || fail 'invalid PID or duplicate-install contract'

# Two genuine children: duplicate registration must cause only one TERM per
# child, cleanup must preserve exit 37, and evidence must survive unchanged.
printf 'unmodified UART evidence\n' > "$test_dir/first.serial.log"
printf 'unmodified NIC capture\n' > "$test_dir/first.pcap"
if sh -c '
    set -eu
    . "$1"
    term_log=$2
    kill() {
        if [ "$1" = -TERM ]; then printf "%s\n" "$2" >> "$term_log"; fi
        command kill "$@"
    }
    nas_acceptance_host_supervisor_install
    (exec sleep 30) &
    first=$!
    (exec sleep 30) &
    second=$!
    printf "%s\n%s\n" "$first" "$second" > "$3"
    nas_acceptance_host_supervisor_register "$first"
    nas_acceptance_host_supervisor_register "$first"
    nas_acceptance_host_supervisor_register "$second"
    exit 37
' sh "$module" "$test_dir/term.log" "$test_dir/children"; then
    fail 'a deliberately failed acceptance script returned success'
else
    test "$?" -eq 37 || fail 'cleanup changed the original exit status'
fi
test "$(wc -l < "$test_dir/term.log")" -eq 2 ||
    fail 'each distinct child must receive TERM exactly once'
while IFS= read -r pid; do
    test "$(grep -Fxc "$pid" "$test_dir/term.log")" -eq 1 ||
        fail 'duplicate registration or missed child cleanup'
    if kill -0 "$pid" 2>/dev/null; then fail 'registered child leaked'; fi
done < "$test_dir/children"
test "$(cat "$test_dir/first.serial.log")" = 'unmodified UART evidence' ||
    fail 'UART evidence was deleted or changed'
test "$(cat "$test_dir/first.pcap")" = 'unmodified NIC capture' ||
    fail 'packet capture was deleted or changed'

# Explicit unregister removes a previously waited child; an idempotent
# manual cleanup may be followed safely by the automatic EXIT cleanup.
sh -c '
    set -eu
    . "$1"
    nas_acceptance_host_supervisor_install
    (exec sleep 30) &
    child=$!
    nas_acceptance_host_supervisor_register "$child"
    # Let the asynchronous subshell exec before signaling.  Killing its PID
    # before exec can race shell job setup and make wait block for 30 seconds.
    sleep 1
    command kill -TERM "$child"
    wait "$child" 2>/dev/null || :
    nas_acceptance_host_supervisor_unregister "$child"
    test -z "$nas_acceptance_host_supervisor_pids"
    nas_acceptance_host_supervisor_cleanup
    nas_acceptance_host_supervisor_cleanup
    if nas_acceptance_host_supervisor_register "$child"; then exit 19; fi
' sh "$module" || fail 'unregister/idempotency contract'

# TERM must pass through the EXIT owner and retain shell convention 128+15.
if sh -c '
    set -eu
    . "$1"
    nas_acceptance_host_supervisor_install
    (exec sleep 30) &
    child=$!
    printf "%s\n" "$child" > "$2"
    nas_acceptance_host_supervisor_register "$child"
    command kill -TERM "$$"
    exit 0
' sh "$module" "$test_dir/signal-child"; then
    fail 'TERM was swallowed'
else
    test "$?" -eq 143 || fail 'TERM exit status was corrupted'
fi
pid=$(cat "$test_dir/signal-child")
if kill -0 "$pid" 2>/dev/null; then fail 'TERM path leaked an observer'; fi
test_elapsed=$(($(date +%s)-test_started))
test "$test_elapsed" -lt 12 || fail 'cleanup exceeded bounded shutdown time'

printf '%s\n' '[ACCEPTANCE SUPERVISOR PASS] invalid PID rejection, two-child dedup, exit 37, durable logs/PCAP, unregister, repeated cleanup, TERM 143'
