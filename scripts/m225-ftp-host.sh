#!/bin/sh
# Source into M197 when M225 pilot is selected; host sources never enter CPIO.
m225_host_pid=

m225_stop_host() {
    [ -n "$m225_host_pid" ] && kill -0 "$m225_host_pid" 2>/dev/null || {
        echo '[M225 HOST FAIL] independent FTP server absent/died' >&2; return 1;
    }
    kill "$m225_host_pid" 2>/dev/null || :
    wait "$m225_host_pid" 2>/dev/null || :
    m225_host_pid=
}

m225_cleanup() {
    if [ -n "$m225_host_pid" ]; then
        kill "$m225_host_pid" 2>/dev/null || :
        wait "$m225_host_pid" 2>/dev/null || :
        m225_host_pid=
    fi
}

m225_start_host() {
    m225_phase=$1
    case "$m225_phase" in first|reopen) ;; *) return 2 ;; esac
    [ -z "$m225_host_pid" ] || { echo '[M225 HOST FAIL] stale server'; return 1; }
    m225_dir="$PWD/$work/m225-host-$m225_phase"
    m225_log="$PWD/$work/m225-$m225_phase-host-ftp.log"
    [ ! -e "$m225_log" ] && [ ! -L "$m225_log" ] || {
        echo '[M225 HOST FAIL] stale host TCP log'; return 1;
    }
    python3 scripts/m225-host-services.py generate "$m225_phase" "$m225_dir"
    python3 -u scripts/m225-host-services.py serve "$m225_phase" "$m225_dir" \
        > "$m225_log" 2>&1 &
    m225_host_pid=$!
    m225_ready=0
    for m225_attempt in 1 2 3 4 5 6 7 8 9 10; do
        if grep -Fxq "[M225 HOST READY] phase=$m225_phase ftp=15451 rest=15452" "$m225_log" 2>/dev/null; then
            m225_ready=1
            break
        fi
        kill -0 "$m225_host_pid" 2>/dev/null || break
        sleep 1
    done
    [ "$m225_ready" -eq 1 ] || {
        echo '[M225 HOST FAIL] original FTP service not ready' >&2
        tail -15 "$m225_log" >&2 || :
        return 1
    }
}
