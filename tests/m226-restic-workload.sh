#!/bin/sh
# Actual signed Alpine /usr/bin/restic driving the official rest-server REST
# protocol: init, remote backup, independent restore, snapshots and check.
# All PASS records remain provisional until host PCAP, server repository,
# signed APK SHA and physical ext4 have been verified outside native QEMU.
set -eu
test "$#" -eq 1 || exit 2
case "$1:${NAS_M226_BOOT_PHASE:-}" in
    --first:first) phase=first ;;
    --reopen:reopen) phase=reopen ;;
    *) echo '[M226 GUEST FAIL] invalid native boot phase' >&2; exit 2 ;;
esac
busy=/bin/busybox
test -x /usr/bin/restic && /sbin/apk info -e restic >/dev/null || {
    echo '[M226 GUEST FAIL] original signed Alpine restic absent' >&2; exit 1;
}
magic=$($busy od -An -tx1 -N4 /usr/bin/restic | $busy tr -d ' \t\r\n')
test "$magic" = 7f454c46 || exit 1
root=/root/m226-work
work="$root/$phase"
test ! -e "$work" || {
    echo '[M226 GUEST FAIL] stale boot challenge or restore' >&2; exit 1;
}
mkdir -p "$work"
elapsed=0
until $busy grep -q '^nameserver 10\.0\.2\.3' /proc/net/naseed-resolv.conf 2>/dev/null; do
    test "$elapsed" -lt 90 || { echo '[M226 GUEST FAIL] DHCP missing' >&2; exit 1; }
    elapsed=$((elapsed+1))
    $busy sleep 1
done

# Origin data is generated only on the host AFTER guest disk staging and
# reaches the guest over its real VirtIO/slirp TCP stack; no preseeded payload.
$busy timeout 90 $busy wget -q -O "$work/source.bin" \
    http://10.0.2.2:15453/m226.source
test -s "$work/source.bin"

export RESTIC_REPOSITORY=rest:http://10.0.2.2:15452/repo/
export RESTIC_PASSWORD=m226-disposable-fixture-only-not-a-real-credential
export RESTIC_CACHE_DIR="$root/cache"
mkdir -p "$RESTIC_CACHE_DIR"

run_restic() {
    step=$1
    shift
    $busy timeout 210 /usr/bin/restic --no-cache "$@" > "$work/$step.log" 2>&1 || {
        $busy tail -n 20 "$work/$step.log" >&2 || :
        echo "[M226 GUEST FAIL] real restic $step" >&2
        return 1
    }
}

if [ "$phase" = first ]; then
    run_restic init init
else
    # Validate the first real remote snapshot survived a separate host server
    # lifecycle and second independent QEMU before creating the new snapshot.
    run_restic preexisting-snapshots snapshots
    run_restic restore-first restore latest --path "$root/first/source.bin" \
        --target "$work/restore-first"
    $busy cmp -s "$root/first/source.bin" \
        "$work/restore-first/root/m226-work/first/source.bin" || {
            echo '[M226 GUEST FAIL] remote first snapshot did not restore' >&2; exit 1;
        }
fi

run_restic backup backup "$work/source.bin" --tag "m226-$phase"
run_restic snapshots snapshots
run_restic restore-current restore latest --path "$work/source.bin" \
    --target "$work/restore-current"
$busy cmp -s "$work/source.bin" \
    "$work/restore-current/root/m226-work/$phase/source.bin" || {
        echo '[M226 GUEST FAIL] phase-selected remote restore bytes mismatch' >&2; exit 1;
    }
run_restic check check
sync
echo "[M226 ORIGINAL RESTIC GUEST] phase=$phase real_init_backup_restore_check original_signed_ELF"
