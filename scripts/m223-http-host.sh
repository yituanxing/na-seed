#!/bin/sh
# Sourced by the M197 owner; ephemeral host-only random source and HTTP server.
m223_host_pid=
m223_stop_host() {
    if [ -n "$m223_host_pid" ]; then
        kill "$m223_host_pid" 2>/dev/null || :
        wait "$m223_host_pid" 2>/dev/null || :
        m223_host_pid=
    fi
}
m223_cleanup() {
    m223_stop_host
}
m223_start_host() {
    m223_phase=$1
    case "$m223_phase" in first|reopen) ;; *) echo '[M223 HOST FAIL] invalid phase' >&2; exit 2 ;; esac
    m223_hostdir="$PWD/$work/m223-host-$m223_phase"
    m223_log="$PWD/$work/m223-$m223_phase-host-http.log"
    test ! -e "$m223_log" || { echo '[M223 HOST FAIL] stale host log' >&2; exit 1; }
    python3 scripts/m223-http-server.py generate "$m223_phase" "$m223_hostdir"
    python3 -u scripts/m223-http-server.py serve "$m223_phase" "$m223_hostdir" > "$m223_log" 2>&1 &
    m223_host_pid=$!
    m223_ready=0
    for m223_attempt in 1 2 3 4 5 6; do
        if grep -Fxq "[M223 HOST READY] phase=$m223_phase port=15431" "$m223_log"; then
            m223_ready=1
            break
        fi
        kill -0 "$m223_host_pid" 2>/dev/null || break
        sleep 1
    done
    if [ "$m223_ready" -ne 1 ]; then
        echo "[M223 HOST FAIL] independent $m223_phase HTTP server not ready" >&2
        tail -20 "$m223_log" >&2 || :
        exit 1
    fi
}
