#!/bin/sh
# Source in the M197 coordinator ONLY for the M226 signed restic pilot.
# The real upstream rest-server owns a repository persisted between two QEMUs.
M226_REST_SERVER_ARCHIVE_SHA256=4c9c95bc079a0334e81fad379b19dc5c3353c71c2c88d652cafce2081c2b1c66
M226_RESTIC_CLIENT_ARCHIVE_SHA256=f415415624dcc452f2a02b8c33641791a8c6d6d3b65bbb3543fcf9a25151585c
m226_source_pid=
m226_repo_pid=

m226_install_rest_server() {
    m226_bin="$PWD/build/deps/m226-rest-server/rest-server_0.14.0_linux_amd64/rest-server"
    if [ ! -x "$m226_bin" ]; then
        mkdir -p "$PWD/build/deps/m226-rest-server"
        m226_archive="$PWD/build/deps/m226-rest-server/rest-server_0.14.0_linux_amd64.tar.gz"
        if [ ! -s "$m226_archive" ]; then
            curl --fail --location --retry 3 --max-time 90 \
                --output "$m226_archive.tmp" \
                https://github.com/restic/rest-server/releases/download/v0.14.0/rest-server_0.14.0_linux_amd64.tar.gz
            mv "$m226_archive.tmp" "$m226_archive"
        fi
        printf '%s  %s\n' "$M226_REST_SERVER_ARCHIVE_SHA256" "$m226_archive" | sha256sum -c -
        tar -xzf "$m226_archive" -C "$PWD/build/deps/m226-rest-server" \
            rest-server_0.14.0_linux_amd64/rest-server
        chmod +x "$m226_bin"
    fi
    # Hash provenance stays mandatory even when the build/deps artifact exists.
    printf '%s  %s\n' "$M226_REST_SERVER_ARCHIVE_SHA256" \
        "$PWD/build/deps/m226-rest-server/rest-server_0.14.0_linux_amd64.tar.gz" | sha256sum -c -
    "$m226_bin" --version | grep -Fq '0.14.0' || {
        echo '[M226 HOST FAIL] not official rest-server v0.14.0' >&2; return 1;
    }
    m226_client="$PWD/build/deps/m226-rest-server/restic_0.19.1_linux_amd64"
    m226_client_archive="$m226_client.bz2"
    if [ ! -s "$m226_client_archive" ]; then
        curl --fail --location --retry 3 --max-time 90 \
            --output "$m226_client_archive.tmp" \
            https://github.com/restic/restic/releases/download/v0.19.1/restic_0.19.1_linux_amd64.bz2
        mv "$m226_client_archive.tmp" "$m226_client_archive"
    fi
    printf '%s  %s\n' "$M226_RESTIC_CLIENT_ARCHIVE_SHA256" "$m226_client_archive" | sha256sum -c -
    if [ ! -x "$m226_client" ]; then
        bzip2 -dc "$m226_client_archive" > "$m226_client"
        chmod +x "$m226_client"
    fi
    "$m226_client" version | grep -Fq 'restic 0.19.1' || {
        echo '[M226 HOST FAIL] unexpected official restic CLI version' >&2; return 1;
    }
}

m226_host_check() {
    m226_verify_phase=$1
    case "$m226_verify_phase" in first|reopen) ;; *) return 2 ;; esac
    m226_verify_target="$PWD/$work/m226-host-restored-$m226_verify_phase"
    test ! -e "$m226_verify_target" || {
        echo '[M226 HOST FAIL] stale independent host restore' >&2; return 1;
    }
    export RESTIC_REPOSITORY="$PWD/$work/m226-rest-server-storage/repo"
    export RESTIC_PASSWORD=m226-disposable-fixture-only-not-a-real-credential
    "$m226_client" --no-cache check --read-data \
        > "$PWD/$work/m226-$m226_verify_phase-host-restic-check.log" 2>&1
    "$m226_client" --no-cache snapshots --json \
        > "$PWD/$work/m226-$m226_verify_phase-host-snapshots.json"
    "$m226_client" --no-cache restore latest \
        --path "/root/m226-work/$m226_verify_phase/source.bin" \
        --target "$m226_verify_target" \
        > "$PWD/$work/m226-$m226_verify_phase-host-restore.log" 2>&1
    cmp -s "$PWD/$work/m226-host-$m226_verify_phase/source.bin" \
        "$m226_verify_target/root/m226-work/$m226_verify_phase/source.bin" || {
        echo '[M226 HOST FAIL] independent original host restic restore mismatch' >&2; return 1;
    }
    echo "[M226 HOST ORIGINAL RESTIC READ-DATA PASS] phase=$m226_verify_phase"
}

m226_start_host() {
    m226_phase=$1
    case "$m226_phase" in first|reopen) ;; *) return 2 ;; esac
    [ -z "$m226_source_pid" ] && [ -z "$m226_repo_pid" ] || {
        echo '[M226 HOST FAIL] stale upstream server process' >&2; return 1;
    }
    m226_install_rest_server
    m226_origin="$PWD/$work/m226-host-$m226_phase"
    m226_source_log="$PWD/$work/m226-$m226_phase-source.log"
    m226_repo_log="$PWD/$work/m226-$m226_phase-rest-server.log"
    m226_repo_dir="$PWD/$work/m226-rest-server-storage"
    [ ! -e "$m226_source_log" ] && [ ! -e "$m226_repo_log" ] || {
        echo '[M226 HOST FAIL] reused host provenance log' >&2; return 1;
    }
    if [ "$m226_phase" = first ]; then
        [ ! -e "$m226_repo_dir" ] && [ ! -L "$m226_repo_dir" ] || {
            echo '[M226 HOST FAIL] stale preexisting restic repository' >&2; return 1;
        }
    else
        [ -f "$m226_repo_dir/repo/config" ] && [ ! -L "$m226_repo_dir" ] || {
            echo '[M226 HOST FAIL] second server did not inherit original repository' >&2; return 1;
        }
    fi
    python3 scripts/m226-host-source.py generate "$m226_phase" "$m226_origin"
    mkdir -p "$m226_repo_dir"
    python3 -u scripts/m226-host-source.py serve "$m226_phase" "$m226_origin" \
        > "$m226_source_log" 2>&1 &
    m226_source_pid=$!
    "$m226_bin" --no-auth --path "$m226_repo_dir" --listen 127.0.0.1:15452 \
        --log - > "$m226_repo_log" 2>&1 &
    m226_repo_pid=$!
    m226_ready=0
    for m226_attempt in 1 2 3 4 5 6 7 8 9 10; do
        if grep -Fxq "[M226 SOURCE READY] phase=$m226_phase port=15453" "$m226_source_log" 2>/dev/null && \
            python3 -c 'import socket; s=socket.create_connection(("127.0.0.1",15452),0.6); s.close()' 2>/dev/null; then
            m226_ready=1
            break
        fi
        kill -0 "$m226_source_pid" 2>/dev/null || break
        kill -0 "$m226_repo_pid" 2>/dev/null || break
        sleep 1
    done
    if [ "$m226_ready" -ne 1 ]; then
        echo '[M226 HOST FAIL] source or true rest-server unavailable' >&2
        tail -20 "$m226_source_log" >&2 || :
        tail -20 "$m226_repo_log" >&2 || :
        return 1
    fi
    echo "[M226 GENUINE REST SERVER READY] phase=$m226_phase official_sha256=$M226_REST_SERVER_ARCHIVE_SHA256"
}

m226_stop_host() {
    kill -0 "$m226_source_pid" 2>/dev/null && kill -0 "$m226_repo_pid" 2>/dev/null || {
        echo '[M226 HOST FAIL] independent real server died during native QEMU' >&2; return 1;
    }
    kill "$m226_source_pid" "$m226_repo_pid" 2>/dev/null || :
    wait "$m226_source_pid" 2>/dev/null || :
    wait "$m226_repo_pid" 2>/dev/null || :
    m226_source_pid=
    m226_repo_pid=
}

m226_cleanup() {
    [ -z "$m226_source_pid" ] || { kill "$m226_source_pid" 2>/dev/null || :; wait "$m226_source_pid" 2>/dev/null || :; }
    [ -z "$m226_repo_pid" ] || { kill "$m226_repo_pid" 2>/dev/null || :; wait "$m226_repo_pid" 2>/dev/null || :; }
    m226_source_pid=
    m226_repo_pid=
}
