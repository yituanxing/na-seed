#!/bin/sh
# Signed ORIGINAL Alpine util-linux utmpdump, real target-libc utmp records.
# This tests lossless offline record parsing, NOT live login accounting.
# The helper is an unsigned probe fixture, never an accepted Alpine ELF.
set -eu

verify_text() {
    [ "$#" -eq 2 ] && [ -s "$1" ] || return 1
    file=$1 user=$2
    case "$user" in naseed|changed) ;; *) return 1 ;; esac
    # util-linux v2.29+ utmpdump uses ISO 8601 UTC by default.  Check
    # precisely one complete record and every independently chosen field;
    # a usage banner, empty dump, stale record, or CLI exit 0 is never PASS.
    awk 'END { exit !(NR == 1) }' "$file" &&
        grep -Eq '^\[7\][[:space:]]+\[0*21942\]' "$file" &&
        grep -Fq '[T219]' "$file" &&
        grep -Fq "[$user" "$file" &&
        grep -Fq 'ttyS1' "$file" &&
        grep -Fq 'alpine.test' "$file" &&
        grep -Fq '192.0.2.55' "$file" &&
        grep -Fq '2023-11-14' "$file" &&
        grep -Fq '22:13:20' "$file" &&
        ! grep -Fq "[$( [ "$user" = naseed ] && printf changed || printf naseed )" "$file"
}

if [ "${1:-}" = --verify-text ]; then
    [ "$#" -eq 3 ] || exit 2
    if verify_text "$2" "$3"; then
        echo '[M219 TEXT CONTRACT PASS] one independently specified utmp record'
        exit 0
    fi
    echo '[M219 TEXT CONTRACT FAIL] missing, altered or extra utmp field' >&2
    exit 1
fi

[ "${1:-}" = --exercise ] && [ "$#" -eq 3 ] &&
    [ "$2" = utmp-record-roundtrip ] && [ "$3" = usr/bin/utmpdump ] || {
        echo '[M219 ERROR] usage: --exercise utmp-record-roundtrip usr/bin/utmpdump' >&2
        exit 2
    }

export PATH=/usr/sbin:/sbin:/usr/bin:/bin HOME=/root LC_ALL=C LANG=C TZ=UTC
fixture=/root/m219-fixtures/utmpdump-fixture
if [ ! -x /usr/bin/utmpdump ]; then
    echo '[M219 FAIL] original signed utmpdump is not executable inside installed guest' >&2
    /bin/busybox ls -ld /usr/bin/utmpdump /usr/bin 2>&1 | /bin/busybox tail -n 3 >&2
    /sbin/apk info -W /usr/bin/utmpdump 2>&1 | /bin/busybox head -n 2 >&2
    exit 1
fi
if [ ! -x "$fixture" ]; then
    echo '[M219 FAIL] host-built musl-ABI fixture is not executable inside guest' >&2
    /bin/busybox ls -ld /root/m219-fixtures "$fixture" 2>&1 | /bin/busybox tail -n 3 >&2
    exit 1
fi
magic=$(/bin/busybox od -An -tx1 -N4 /usr/bin/utmpdump 2>/dev/null |
    /bin/busybox tr -d ' \t\r\n')
[ "$magic" = 7f454c46 ] || {
    echo '[M219 FAIL] original Alpine utmpdump is not an ELF' >&2
    exit 1
}
target="/root/m219-work/utmpdump.${NAS_M219_BOOT_PHASE:-manual}"
/bin/busybox mkdir -p "$target" || exit 1

"$fixture" --write "$target/original.utmp" > "$target/fixture.log" || exit 1
"$fixture" --verify-original "$target/original.utmp" || exit 1
/bin/busybox timeout 15 /usr/bin/utmpdump "$target/original.utmp" \
    > "$target/original.txt" 2> "$target/original.err" || {
        echo '[M219 FAIL] original utmpdump cannot decode target-ABI fixture' >&2
        /bin/busybox tail -n 3 "$target/original.err" >&2
        exit 1
    }
if ! verify_text "$target/original.txt" naseed; then
    echo '[M219 FAIL] decoded original utmp fields mismatch independent oracle' >&2
    /bin/busybox tail -n 2 "$target/original.txt" >&2
    exit 1
fi

/bin/busybox timeout 15 /usr/bin/utmpdump -r < "$target/original.txt" \
    > "$target/reversed.utmp" 2> "$target/reversed.err" || {
        echo '[M219 FAIL] original utmpdump -r could not restore binary' >&2
        /bin/busybox tail -n 3 "$target/reversed.err" >&2
        exit 1
    }
# A binary compare alone also enforces every padding/reserved byte and
# native 32/64-bit session and timestamp layout; do not silently relax it.
if ! "$fixture" --verify-original "$target/reversed.utmp"; then
    echo '[M219 FAIL] original utmpdump -r is lossy on target ABI' >&2
    exit 1
fi

# Change ONE genuine decoded field, not the fixture helper's input.  The
# ORIGINAL reverse parser must interpret the edited text and encode a
# different, independently predicted record.  Re-decoding it must agree.
/bin/busybox sed 's/naseed/changed/' "$target/original.txt" \
    > "$target/edited.txt" || exit 1
verify_text "$target/edited.txt" changed || exit 1
/bin/busybox timeout 15 /usr/bin/utmpdump -r < "$target/edited.txt" \
    > "$target/edited.utmp" 2> "$target/edited.err" || {
        echo '[M219 FAIL] original reverse parser ignored edited user field' >&2
        /bin/busybox tail -n 3 "$target/edited.err" >&2
        exit 1
    }
"$fixture" --verify-edited "$target/edited.utmp" || {
    echo '[M219 FAIL] original -r failed independent changed-user byte oracle' >&2
    exit 1
}
/bin/busybox timeout 15 /usr/bin/utmpdump "$target/edited.utmp" \
    > "$target/redecoded.txt" 2> "$target/redecoded.err" || exit 1
verify_text "$target/redecoded.txt" changed || exit 1
echo '[M219 genuine guest semantic PASS] original Alpine utmpdump parsed, edited and losslessly rebuilt native struct utmp bytes'
