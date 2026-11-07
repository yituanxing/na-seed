#!/bin/sh
# Sourced only by M222's physical ext4 pilot. No daemon, key, or hostname is
# reused between independent guest boots. Based on M142's proven host sshd.
m222_host_pid=
m222_host_user=$(id -un)
case "$m222_host_user" in ''|*[!a-zA-Z0-9_-]*)
    echo '[M222 HOST FAIL] unsupported host account name' >&2; exit 1 ;;
esac
m222_stop_host() {
    if [ -n "$m222_host_pid" ]; then
        kill "$m222_host_pid" 2>/dev/null || :
        wait "$m222_host_pid" 2>/dev/null || :
        m222_host_pid=
    fi
}
m222_cleanup() {
    m222_exit_status=$?
    m222_stop_host
    for m222_phase in first reopen; do
        m222_private_dir="$PWD/$work/m222-host-$m222_phase"
        # Exact, locally created secret paths only. Keep sanitized sshd logs,
        # guest UART, source hashes, original ELF inventory and both PCAPs.
        rm -f "$m222_private_dir/client_id_ed25519" \
              "$m222_private_dir/client_id_ed25519.pub" \
              "$m222_private_dir/ssh_host_ed25519_key" \
              "$m222_private_dir/ssh_host_ed25519_key.pub" \
              "$m222_private_dir/authorized_keys" \
              "$m222_private_dir/sshd.pid" \
              "$m222_private_dir/ssh-source" \
              "$m222_private_dir/scp-source" \
              "$m222_private_dir/sftp-source" \
              "$m222_private_dir/scp-up" \
              "$m222_private_dir/scp-echo" \
              "$m222_private_dir/sftp-up" \
              "$m222_private_dir/sftp-echo"
        rmdir "$m222_private_dir" 2>/dev/null || :
        if [ -n "${overlay:-}" ] && [ -d "$overlay/root/m222-auth/$m222_phase" ]; then
            sudo rm -f "$overlay/root/m222-auth/$m222_phase/id_ed25519" \
                "$overlay/root/m222-auth/$m222_phase/id_ed25519.pub" || :
        fi
    done
    if [ "$m222_exit_status" -ne 0 ] && [ -n "${disk:-}" ] && [ -f "$disk" ]; then
        # The disposable disk may still contain a secret if its guest was
        # killed mid-roundtrip; never preserve or upload that raw image.
        rm -f "$disk"
        echo '[M222 SECRET CLEANUP] failed run removed exact private-key-bearing disposable image' >&2
    fi
}
m222_prepare_host() {
    m222_phase=$1 m222_stage_overlay=$2
    case "$m222_phase" in first|reopen) ;; *) return 2 ;; esac
    m222_secret="$PWD/$work/m222-host-$m222_phase"
    test ! -e "$m222_secret" || {
        echo '[M222 HOST FAIL] stale secret directory' >&2; return 1;
    }
    umask 077
    mkdir -m 0700 "$m222_secret"
    command -v ssh-keygen >/dev/null || return 1
    ssh-keygen -q -t ed25519 -N '' -C "m222-$m222_phase-client-temporary" \
        -f "$m222_secret/client_id_ed25519" || return 1
    ssh-keygen -q -t ed25519 -N '' -C "m222-$m222_phase-server-temporary" \
        -f "$m222_secret/ssh_host_ed25519_key" || return 1
    cp "$m222_secret/client_id_ed25519.pub" "$m222_secret/authorized_keys"
    chmod 600 "$m222_secret/authorized_keys"
    ssh-keygen -lf "$m222_secret/ssh_host_ed25519_key.pub" -E sha256 \
        > "$PWD/$work/m222-$m222_phase-public-fingerprint.log"

    m222_guest_auth="$m222_stage_overlay/root/m222-auth/$m222_phase"
    sudo install -d -m 0700 "$m222_guest_auth"
    sudo install -m 0600 "$m222_secret/client_id_ed25519" "$m222_guest_auth/id_ed25519"
    awk '$1=="ssh-ed25519" {print "[10.0.2.2]:15423",$1,$2}' \
        "$m222_secret/ssh_host_ed25519_key.pub" |
        sudo tee "$m222_guest_auth/known_hosts" >/dev/null
    printf '%s %s\n' "$m222_host_user" "$m222_secret" |
        sudo tee "$m222_guest_auth/host-info" >/dev/null
    sudo chmod 0600 "$m222_guest_auth/known_hosts" "$m222_guest_auth/host-info"
    sudo test -s "$m222_guest_auth/known_hosts"

    # Distinct unpredictable sources are HOST-ONLY; guest must fetch them
    # over SSH/SCP/SFTP, then return byte-identical copies for the oracle.
    m222_nonce=$(od -An -tx1 -N24 /dev/urandom | tr -d ' \n')
    printf 'M222_SSH_%s_SOURCE_%s\n' "$m222_phase" "$m222_nonce" \
        > "$m222_secret/ssh-source"
    m222_nonce=$(od -An -tx1 -N24 /dev/urandom | tr -d ' \n')
    printf 'M222_SCP_%s_SOURCE_%s\n' "$m222_phase" "$m222_nonce" \
        > "$m222_secret/scp-source"
    m222_nonce=$(od -An -tx1 -N24 /dev/urandom | tr -d ' \n')
    printf 'M222_SFTP_%s_SOURCE_%s\n' "$m222_phase" "$m222_nonce" \
        > "$m222_secret/sftp-source"
    echo "[M222 HOST PREPARED] phase=$m222_phase separate Ed25519 identities and three unpredictable sources"
}
m222_start_host() {
    m222_phase=$1
    m222_secret="$PWD/$work/m222-host-$m222_phase"
    m222_log="$PWD/$work/m222-$m222_phase-host-sshd.log"
    test -s "$m222_secret/ssh_host_ed25519_key" &&
        test -s "$m222_secret/authorized_keys" && test ! -e "$m222_log" || return 1
    sshd_bin=$(command -v sshd) || return 1
    "$sshd_bin" -D -e -f /dev/null -p 15423 \
        -h "$m222_secret/ssh_host_ed25519_key" \
        -o ListenAddress=127.0.0.1 \
        -o HostKeyAlgorithms=ssh-ed25519 \
        -o PubkeyAuthentication=yes \
        -o "AuthorizedKeysFile=$m222_secret/authorized_keys" \
        -o PasswordAuthentication=no \
        -o KbdInteractiveAuthentication=no \
        -o HostbasedAuthentication=no \
        -o PermitRootLogin=no \
        -o UsePAM=no \
        -o AllowTcpForwarding=no \
        -o AllowAgentForwarding=no \
        -o AllowStreamLocalForwarding=no \
        -o PermitTunnel=no \
        -o X11Forwarding=no \
        -o PermitTTY=no \
        -o 'Subsystem=sftp internal-sftp' \
        -o LogLevel=VERBOSE \
        -o "PidFile=$m222_secret/sshd.pid" \
        > "$m222_log" 2>&1 &
    m222_host_pid=$!
    m222_ready=0
    for m222_attempt in 1 2 3 4 5 6; do
        if python3 -c 'import socket
with socket.create_connection(("127.0.0.1", 15423), 2) as sock:
    sock.settimeout(2)
    assert sock.recv(160).startswith(b"SSH-2.0-OpenSSH_")' >/dev/null 2>&1; then
            m222_ready=1
            break
        fi
        kill -0 "$m222_host_pid" 2>/dev/null || break
        sleep 1
    done
    if [ "$m222_ready" -ne 1 ]; then
        echo '[M222 HOST FAIL] temporary sshd never served real OpenSSH banner' >&2
        tail -16 "$m222_log" >&2 || :
        return 1
    fi
    echo "[M222 HOST READY] phase=$m222_phase real OpenSSH 127.0.0.1:15423"
}
