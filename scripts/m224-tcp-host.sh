#!/bin/sh
# Source from M197: genuine, phase-isolated host TCP witnesses for native
# Alpine Nmap, Ncat, and an unmodified host iperf3 server.
m224_tcp_pid=
m224_iperf_pid=

m224_stop_host() {
    [ -n "$m224_tcp_pid" ] || { echo '[M224 HOST FAIL] missing host TCP process' >&2; return 1; }
    [ -n "$m224_iperf_pid" ] || { echo '[M224 HOST FAIL] missing iperf3 host process' >&2; return 1; }
    kill -0 "$m224_tcp_pid" 2>/dev/null || {
        echo '[M224 HOST FAIL] host TCP witness died during native QEMU' >&2
        return 1
    }
    kill -0 "$m224_iperf_pid" 2>/dev/null || {
        echo '[M224 HOST FAIL] original host iperf3 died during native QEMU' >&2
        return 1
    }
    kill "$m224_tcp_pid" "$m224_iperf_pid" 2>/dev/null || :
    wait "$m224_tcp_pid" 2>/dev/null || :
    wait "$m224_iperf_pid" 2>/dev/null || :
    m224_tcp_pid=
    m224_iperf_pid=
}

m224_cleanup() {
    # The EXIT trap must release even a partly started host. Do not let a
    # broken witness accidentally turn an earlier failure into a success.
    if [ -n "$m224_tcp_pid" ]; then
        kill "$m224_tcp_pid" 2>/dev/null || :
        wait "$m224_tcp_pid" 2>/dev/null || :
        m224_tcp_pid=
    fi
    if [ -n "$m224_iperf_pid" ]; then
        kill "$m224_iperf_pid" 2>/dev/null || :
        wait "$m224_iperf_pid" 2>/dev/null || :
        m224_iperf_pid=
    fi
}

m224_start_host() {
    m224_phase=$1
    case "$m224_phase" in first|reopen) ;; *) echo '[M224 HOST FAIL] invalid phase' >&2; return 2 ;; esac
    [ -z "$m224_tcp_pid" ] && [ -z "$m224_iperf_pid" ] || {
        echo '[M224 HOST FAIL] previous host phase still active' >&2; return 1;
    }
    command -v iperf3 >/dev/null 2>&1 || {
        echo '[M224 HOST FAIL] original host iperf3 is not installed' >&2; return 1;
    }
    m224_hostdir="$PWD/$work/m224-host-$m224_phase"
    m224_log="$PWD/$work/m224-$m224_phase-host-tcp.log"
    [ ! -e "$m224_log" ] && [ ! -L "$m224_log" ] || {
        echo '[M224 HOST FAIL] stale host receipt log' >&2; return 1;
    }
    python3 scripts/m224-tcp-server.py generate "$m224_phase" "$m224_hostdir" || return 1
    python3 -u scripts/m224-tcp-server.py serve "$m224_phase" "$m224_hostdir" \
        > "$m224_log" 2>&1 &
    m224_tcp_pid=$!
    # Reuse the historical successfully proven M141 host iperf3 launcher:
    # QEMU's 10.0.2.2 gateway reaches this loopback-bound host socket.
    stdbuf -oL -eL iperf3 -s -B 127.0.0.1 -p 15444 \
        > "$m224_hostdir/iperf3.log" 2>&1 &
    m224_iperf_pid=$!
    m224_ready=0
    for m224_attempt in 1 2 3 4 5 6 7 8 9 10; do
        if grep -Fxq "[M224 HOST READY] phase=$m224_phase nmap=15441 closed=15442 ncat=15443" "$m224_log" 2>/dev/null && \
           grep -Fq 'Server listening on 15444' "$m224_hostdir/iperf3.log" 2>/dev/null; then
            m224_ready=1
            break
        fi
        kill -0 "$m224_tcp_pid" 2>/dev/null || break
        kill -0 "$m224_iperf_pid" 2>/dev/null || break
        sleep 1
    done
    if [ "$m224_ready" -ne 1 ]; then
        echo "[M224 HOST FAIL] $m224_phase phase TCP/Nmap/Ncat/iperf3 not ready" >&2
        tail -12 "$m224_log" >&2 || :
        tail -12 "$m224_hostdir/iperf3.log" >&2 || :
        return 1
    fi
}
