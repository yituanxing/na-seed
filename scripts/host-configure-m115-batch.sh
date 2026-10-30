#!/bin/sh
# Supply the normal UID/GID 0 identity database on the Linux HOST.  Alpine's
# signed APK overlay deliberately uses --no-scripts; the minimal initramfs
# otherwise lacks /etc/passwd, and upstream ssh-keygen rejects UID 0 with
# "No user exists for uid 0" before attempting real key generation.
set -eu
[ "$#" -eq 2 ] || exit 2
case "$1" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
root=$2
test -d "$root/etc" || exit 1
mkdir -p "$root/root"

passwd="$root/etc/passwd"
if [ ! -e "$passwd" ]; then
    printf 'root:x:0:0:root:/root:/bin/sh\n' > "$passwd"
elif ! grep -Eq '^root:[^:]*:0:0:' "$passwd"; then
    # Keep genuine signed package account entries. Never clobber an existing
    # conflicting UID 0 or GID 0 identity while completing minirootfs data.
    if grep -Eq '^[^:]+:[^:]*:0:' "$passwd"; then exit 1; fi
    printf 'root:x:0:0:root:/root:/bin/sh\n' >> "$passwd"
fi
grep -Eq '^root:[^:]*:0:0:' "$passwd" || {
    echo '[M115 HOST IDENTITY FAIL] missing valid root uid/gid entry' >&2
    exit 1
}

group="$root/etc/group"
if [ ! -e "$group" ]; then
    printf 'root:x:0:\n' > "$group"
elif ! grep -Eq '^root:[^:]*:0:' "$group"; then
    if grep -Eq '^[^:]+:[^:]*:0:' "$group"; then exit 1; fi
    printf 'root:x:0:\n' >> "$group"
fi
grep -Eq '^root:[^:]*:0:' "$group" || {
    echo '[M115 HOST IDENTITY FAIL] missing valid root group entry' >&2
    exit 1
}
echo '[M115 HOST IDENTITY PASS] port-specific root passwd/group, no guest installer'
