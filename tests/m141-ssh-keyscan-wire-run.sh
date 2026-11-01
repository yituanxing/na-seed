#!/bin/sh
# Untouched signed Alpine OpenSSH client on a real VirtIO/lwIP TCP path.
# The host public key is not stored in the guest image; the host oracle
# later compares this observed SSH key against an ephemeral generated key.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
LC_ALL=C
LANG=C
export PATH LC_ALL LANG
busy() { /bin/busybox "$@"; }
work=/tmp/m141-ssh-keyscan-wire
busy mkdir -p "$work"
HOME="$work"
export HOME

exe=/usr/bin/ssh-keyscan
[ -x "$exe" ] || exit 1
resolved=$(busy readlink -f "$exe") || exit 1
case "$resolved" in /bin/busybox|/usr/bin/busybox) exit 1 ;; esac
magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
        busy od -An -tx1 | busy tr -d ' \n')
[ "$magic" = 7f454c46 ] || exit 1

scanned=false
for attempt in 1 2 3 4; do
    busy timeout 12 "$exe" -4 -T 4 -p 15422 -t ed25519 \
      10.0.2.2 > "$work/keyscan" 2> "$work/error" || :
    # A non-default port must appear as [host]:port in OpenSSH known_hosts
    # format. Parse the actual SSH-ED25519 key, not the banner alone.
    if busy grep -Eq '^\[10\.0\.2\.2\]:15422[[:space:]]+ssh-ed25519[[:space:]]+[A-Za-z0-9+/=]+$' \
         "$work/keyscan"; then
        scanned=true
        break
    fi
    busy sleep 1
done
if [ "$scanned" != true ]; then
    busy printf '[M141 SSH KEYSCAN DIAG] signed Alpine ssh-keyscan failed\n'
    busy head -n 12 "$work/keyscan" || :
    busy head -n 12 "$work/error" || :
    exit 1
fi

public_blob=$(busy awk '$2=="ssh-ed25519" {print $3; exit}' "$work/keyscan")
case "$public_blob" in ''|*[!A-Za-z0-9+/=]*) exit 1 ;; esac
busy printf 'M141_SIGNED_SSH_KEYSCAN_PUBLIC %s\n' "$public_blob"
busy printf 'M141_SIGNED_SSH_KEYSCAN_WIRE_PASS\n'
