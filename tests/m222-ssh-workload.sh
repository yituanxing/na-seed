#!/bin/sh
# M142's REAL OpenSSH command/SCP/SFTP host round trips, now one signed ELF
# per M205 ledger row, on M197 physical ext4 first and independent reopen.
# Host-generated, phase-unique source bytes are never preseeded in the guest.
set -eu
test "${1:-}" = --exercise && test "$#" -eq 3 || exit 2
case "${NAS_M222_BOOT_PHASE:-}" in
    first|reopen) phase=$NAS_M222_BOOT_PHASE ;;
    *) echo '[M222 SSH FAIL] invalid native QEMU boot phase' >&2; exit 2 ;;
esac
export PATH=/usr/sbin:/sbin:/usr/bin:/bin LC_ALL=C LANG=C HOME=/root
busy() { /bin/busybox "$@"; }
auth="/root/m222-auth/$phase"
key="$auth/id_ed25519"
known="$auth/known_hosts"
work="/root/m222-work/$2.$phase"
test -s "$key" && test -s "$known" && test -s "$auth/host-info"
test "$(busy stat -c %a "$key")" = 600 || exit 1
IFS=' ' read -r host_user host_dir < "$auth/host-info"
case "$host_user" in ''|*[!a-zA-Z0-9_-]*) exit 1 ;; esac
case "$host_dir" in /*) ;; *) exit 1 ;; esac
case "$host_dir" in *' '*|*'..'*|*"'"*) exit 1 ;; esac
host="$host_user@10.0.2.2"
test -x "/$3"
magic=$(busy od -An -tx1 -N4 "/$3" 2>/dev/null | busy tr -d ' \n\t')
test "$magic" = 7f454c46 || exit 1
busy mkdir -p "$work"

# The independent second CPIO QEMU may execute before lwIP's address
# conflict/DHCP phases settle; no unconditional sleeps or fake lease markers.
waited=0
until busy grep -q '^nameserver 10\.0\.2\.3' /proc/net/naseed-resolv.conf 2>/dev/null; do
    if [ "$waited" -ge 90 ]; then
        echo "[M222 DHCP FAIL] phase=$phase no lease-backed route in 90s" >&2
        exit 1
    fi
    waited=$((waited+1))
    busy sleep 1
done

case "$2:$3" in
    authenticated-host-command:usr/bin/ssh)
        # M142's retries may cover only route/timeout while DHCP converges.
        ready=0
        for attempt in 1 2 3 4 5 6 7 8 9 10 11 12; do
            if busy timeout 20 /usr/bin/ssh -F /dev/null -i "$key" -p 15423 \
                -o BatchMode=yes -o IdentitiesOnly=yes -o IdentityAgent=none \
                -o StrictHostKeyChecking=yes -o UserKnownHostsFile="$known" \
                -o GlobalKnownHostsFile=/dev/null -o ConnectTimeout=4 \
                "$host" "cat '$host_dir/ssh-source'" \
                > "$work/ssh.stdout" 2> "$work/ssh.stderr"; then
                ready=1
                break
            fi
            if ! busy grep -Eq 'Host is unreachable|Network is unreachable|Connection timed out' \
                    "$work/ssh.stderr"; then
                break
            fi
            busy sleep 1
        done
        if [ "$ready" -ne 1 ] || [ ! -s "$work/ssh.stdout" ]; then
            busy head -n 8 "$work/ssh.stderr" >&2 || :
            exit 1
        fi
        # Not counted as independent wire proof: host separately checks the
        # actual SSH output bytes on physical ext4 against its random source.
        echo "M222_ORIGINAL_SSH_STDOUT_CLI_PASS phase=$phase"
        ;;
    authenticated-scp-file-roundtrip:usr/bin/scp)
        busy printf 'M222_REAL_SCP_GUEST_UPLOAD %s\n' "$phase" > "$work/scp-up"
        busy timeout 45 /usr/bin/scp -F /dev/null -i "$key" -P 15423 \
            -o BatchMode=yes -o IdentitiesOnly=yes -o IdentityAgent=none \
            -o StrictHostKeyChecking=yes -o UserKnownHostsFile="$known" \
            -o GlobalKnownHostsFile=/dev/null \
            "$work/scp-up" "$host:$host_dir/scp-up" \
            > "$work/scp.stdout" 2> "$work/scp.stderr" || {
                busy head -n 10 "$work/scp.stderr" >&2 || :; exit 1;
            }
        busy timeout 45 /usr/bin/scp -F /dev/null -i "$key" -P 15423 \
            -o BatchMode=yes -o IdentitiesOnly=yes -o IdentityAgent=none \
            -o StrictHostKeyChecking=yes -o UserKnownHostsFile="$known" \
            -o GlobalKnownHostsFile=/dev/null \
            "$host:$host_dir/scp-source" "$work/scp-down" \
            >> "$work/scp.stdout" 2>> "$work/scp.stderr" || {
                busy head -n 10 "$work/scp.stderr" >&2 || :; exit 1;
            }
        test -s "$work/scp-down" || exit 1
        busy timeout 45 /usr/bin/scp -F /dev/null -i "$key" -P 15423 \
            -o BatchMode=yes -o IdentitiesOnly=yes -o IdentityAgent=none \
            -o StrictHostKeyChecking=yes -o UserKnownHostsFile="$known" \
            -o GlobalKnownHostsFile=/dev/null \
            "$work/scp-down" "$host:$host_dir/scp-echo" \
            >> "$work/scp.stdout" 2>> "$work/scp.stderr" || {
                busy head -n 10 "$work/scp.stderr" >&2 || :; exit 1;
            }
        echo "M222_ORIGINAL_SCP_CLI_PASS phase=$phase"
        ;;
    authenticated-sftp-file-roundtrip:usr/bin/sftp)
        busy printf 'M222_REAL_SFTP_GUEST_UPLOAD %s\n' "$phase" > "$work/sftp-up"
        busy printf 'put %s %s/sftp-up\nget %s/sftp-source %s\nput %s %s/sftp-echo\n' \
            "$work/sftp-up" "$host_dir" "$host_dir" "$work/sftp-down" \
            "$work/sftp-down" "$host_dir" > "$work/sftp.batch"
        busy timeout 70 /usr/bin/sftp -F /dev/null -b "$work/sftp.batch" \
            -i "$key" -P 15423 -o BatchMode=yes -o IdentitiesOnly=yes \
            -o IdentityAgent=none -o StrictHostKeyChecking=yes \
            -o UserKnownHostsFile="$known" -o GlobalKnownHostsFile=/dev/null \
            "$host" > "$work/sftp.stdout" 2> "$work/sftp.stderr" || {
                busy head -n 12 "$work/sftp.stderr" >&2 || :; exit 1;
            }
        test -s "$work/sftp-down" || exit 1
        # The final original client erases this phase's private key BEFORE
        # any final guest sync or boot teardown. Reopen has a different key.
        busy rm -f "$key" "$auth/id_ed25519.pub" || exit 1
        test ! -e "$key" && test ! -e "$auth/id_ed25519.pub" || exit 1
        echo "M222_ORIGINAL_SFTP_CLI_PASS phase=$phase key=deleted"
        ;;
    *) echo '[M222 SSH FAIL] wrong signed ELF/probe pair' >&2; exit 2 ;;
esac
