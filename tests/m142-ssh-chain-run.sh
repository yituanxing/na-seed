#!/bin/sh
# Three untouched signed Alpine OpenSSH ELFs on ONE real VirtIO/lwIP guest:
# SSH remote command, SCP upload/download, SFTP batch upload/download.
# Only a per-run generated key enters this temporary CPIO, never credentials.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
LC_ALL=C
LANG=C
export PATH LC_ALL LANG
busy() { /bin/busybox "$@"; }
work=/tmp/m142-ssh-chain
busy mkdir -p "$work"
HOME="$work"
export HOME

for exe in /usr/bin/ssh /usr/bin/scp /usr/bin/sftp; do
    [ -x "$exe" ] || exit 1
    resolved=$(busy readlink -f "$exe") || exit 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) exit 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || exit 1
done
test -s /tests/ssh-chain/id_ed25519
test -s /tests/ssh-chain/known_hosts
IFS=' ' read -r host_user host_dir < /tests/ssh-chain/host-info
case "$host_user" in ''|*[!a-zA-Z0-9_-]*) exit 1 ;; esac
case "$host_dir" in /*) ;; *) exit 1 ;; esac
case "$host_dir" in *' '*|*'..'*) exit 1 ;; esac
host="$host_user@10.0.2.2"

# The original QEMU wire capture contains a valid DHCP OFFER/ACK but some
# boots are still in lwIP IPv4 address-conflict detection after the initial
# four-second delay. Retry only routing/timeout errors while the real DHCP
# lease settles. Authentication or protocol failures are never masked.
ssh_ready=false
for attempt in 1 2 3 4 5 6 7 8 9 10 11 12; do
    if busy timeout 15 /usr/bin/ssh -F /dev/null \
        -i /tests/ssh-chain/id_ed25519 -p 15423 \
        -o BatchMode=yes -o IdentitiesOnly=yes -o IdentityAgent=none \
        -o StrictHostKeyChecking=yes \
        -o UserKnownHostsFile=/tests/ssh-chain/known_hosts \
        -o GlobalKnownHostsFile=/dev/null \
        -o ConnectTimeout=4 "$host" \
        'printf M142_REAL_SSH_REMOTE_COMMAND_OK' \
        > "$work/ssh.out" 2> "$work/ssh.err"; then
        ssh_ready=true
        break
    fi
    if ! busy grep -Eq 'Host is unreachable|Network is unreachable|Connection timed out' \
        "$work/ssh.err"; then
        break
    fi
    busy sleep 1
done
if [ "$ssh_ready" != true ]; then
    busy head -n 16 "$work/ssh.err" || :
    exit 1
fi
busy grep -Fxq M142_REAL_SSH_REMOTE_COMMAND_OK "$work/ssh.out"
busy printf 'M142_SIGNED_SSH_COMMAND_PASS\n'

# Modern original OpenSSH scp uses the remote SFTP subsystem by default.
# Return the downloaded host data to a SECOND remote path, allowing the
# host oracle to compare bytes without planting its random source in CPIO.
busy printf 'M142_REAL_SCP_GUEST_UPLOAD\n' > "$work/scp-up"
busy timeout 28 /usr/bin/scp -F /dev/null \
    -i /tests/ssh-chain/id_ed25519 -P 15423 \
    -o BatchMode=yes -o IdentitiesOnly=yes -o IdentityAgent=none \
    -o StrictHostKeyChecking=yes \
    -o UserKnownHostsFile=/tests/ssh-chain/known_hosts \
    -o GlobalKnownHostsFile=/dev/null \
    "$work/scp-up" "$host:$host_dir/scp-up" \
    > "$work/scp.out" 2> "$work/scp.err" || {
        busy head -n 16 "$work/scp.err" || :; exit 1;
    }
busy timeout 28 /usr/bin/scp -F /dev/null \
    -i /tests/ssh-chain/id_ed25519 -P 15423 \
    -o BatchMode=yes -o IdentitiesOnly=yes -o IdentityAgent=none \
    -o StrictHostKeyChecking=yes \
    -o UserKnownHostsFile=/tests/ssh-chain/known_hosts \
    -o GlobalKnownHostsFile=/dev/null \
    "$host:$host_dir/scp-source" "$work/scp-down" \
    >> "$work/scp.out" 2>> "$work/scp.err" || {
        busy head -n 16 "$work/scp.err" || :; exit 1;
    }
test -s "$work/scp-down"
busy timeout 28 /usr/bin/scp -F /dev/null \
    -i /tests/ssh-chain/id_ed25519 -P 15423 \
    -o BatchMode=yes -o IdentitiesOnly=yes -o IdentityAgent=none \
    -o StrictHostKeyChecking=yes \
    -o UserKnownHostsFile=/tests/ssh-chain/known_hosts \
    -o GlobalKnownHostsFile=/dev/null \
    "$work/scp-down" "$host:$host_dir/scp-echo" \
    >> "$work/scp.out" 2>> "$work/scp.err" || {
        busy head -n 16 "$work/scp.err" || :; exit 1;
    }
busy printf 'M142_SIGNED_SCP_ROUNDTRIP_PASS\n'

busy printf 'M142_REAL_SFTP_GUEST_UPLOAD\n' > "$work/sftp-up"
busy printf 'put %s %s/sftp-up\nget %s/sftp-source %s\nput %s %s/sftp-echo\n' \
    "$work/sftp-up" "$host_dir" "$host_dir" "$work/sftp-down" \
    "$work/sftp-down" "$host_dir" > "$work/sftp.batch"
busy timeout 40 /usr/bin/sftp -F /dev/null \
    -b "$work/sftp.batch" -i /tests/ssh-chain/id_ed25519 -P 15423 \
    -o BatchMode=yes -o IdentitiesOnly=yes -o IdentityAgent=none \
    -o StrictHostKeyChecking=yes \
    -o UserKnownHostsFile=/tests/ssh-chain/known_hosts \
    -o GlobalKnownHostsFile=/dev/null \
    "$host" > "$work/sftp.out" 2> "$work/sftp.err" || {
        busy head -n 18 "$work/sftp.err" || :; exit 1;
    }
test -s "$work/sftp-down"
busy printf 'M142_SIGNED_SFTP_ROUNDTRIP_PASS\n'
