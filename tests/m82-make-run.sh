#!/bin/sh
# M82: the ORIGINAL signed Alpine GNU Make does real dependency tracking on
# three independently mounted writable VFS providers. A build command that
# merely exits zero is not accepted: verify bytes, recipe counts and parent
# directory timestamps after actual namespace mutations.
#
# The existing M80 Alpine installer and M81 three-FS QEMU boot are reused.
# This is intentionally not a replacement test harness or a Make clone.

PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME LC_ALL LANG

original_make() {
    [ -x /usr/bin/make ] || { echo '[M82 MISSING ORIGINAL MAKE]' >&2; return 1; }
    resolved=$(/bin/busybox readlink -f /usr/bin/make) || return 1
    case "$resolved" in
      /bin/busybox|/usr/bin/busybox)
        echo '[M82 MAKE IS BUSYBOX]' >&2; return 1 ;;
    esac
    magic=$(/bin/busybox dd if="$resolved" bs=1 count=4 2>/dev/null |
           /bin/busybox od -An -tx1 | /bin/busybox tr -d ' \n')
    [ "$magic" = 7f454c46 ] || {
        echo '[M82 MAKE IS NOT ELF]' >&2
        return 1
    }
    /usr/bin/make --version | /bin/busybox grep -q '^GNU Make '
}

make_run() {
    /usr/bin/make --no-print-directory -rR -C "$1" -f Makefile all
}

recipe_count() {
    /bin/busybox wc -c < "$1/runs"
}

prepare_pipeline() {
    work=$1
    /bin/busybox mkdir -p "$work"
    printf 'alpha one\n' > "$work/source.txt"
    # Both transformation commands are genuine unmodified Alpine programs.
    # Two independent recipes must execute in order, then become up-to-date.
    /bin/busybox cat > "$work/Makefile" <<'MAKE'
all: result.gz
result.gz: result.txt
	/bin/gzip -n -c result.txt > result.gz
	printf G >> runs
result.txt: source.txt
	/bin/sed 's/alpha/ALPHA/; s/beta/BETA/' source.txt > result.txt
	printf S >> runs
MAKE
}

assert_pipeline() {
    work=$1
    expected=$2
    /bin/gzip -dc "$work/result.gz" > "$work/decompressed"
    printf '%s\n' "$expected" > "$work/expected"
    /bin/busybox cmp "$work/decompressed" "$work/expected"
}

prepare_directory() {
    work=$1
    /bin/busybox mkdir -p "$work/inputs"
    printf 'a\n' > "$work/inputs/a"
    case "${2:-}" in
      extra-file) printf 'b\n' > "$work/inputs/b" ;;
      extra-dir) /bin/busybox mkdir "$work/inputs/empty" ;;
      '') ;;
      *) return 2 ;;
    esac
    /bin/busybox cat > "$work/Makefile" <<'MAKE'
all: snapshot
snapshot: inputs
	/bin/busybox ls -1 inputs > snapshot
	printf D >> runs
MAKE
    make_run "$work"
    [ "$(recipe_count "$work")" -eq 1 ]
}

# A zero-epoch baseline plus a COPY of its exact timestamp to the generated
# target ensures that the warm Make run is a no-op. A namespace mutation must
# then advance the directory by at least one nanosecond. This works even on
# firmware without a trustworthy RTC, where NaSeed uses monotonic nanoseconds
# since boot instead of assuming the host's calendar year.
stamp_directory_baseline() {
    work=$1
    /bin/busybox touch -t 197001010000.00 "$work/inputs"
    /bin/busybox touch -r "$work/inputs" "$work/snapshot"
    before=$(/bin/busybox stat -c %y "$work/inputs")
    after=$(/bin/busybox stat -c %y "$work/snapshot")
    [ "$before" = "$after" ]
    make_run "$work"
    [ "$(recipe_count "$work")" -eq 1 ]
}

assert_directory_rebuilt() {
    work=$1
    expected=$2
    after=$(/bin/busybox stat -c %y "$work/inputs")
    [ "$after" != "$before" ]
    make_run "$work"
    [ "$(recipe_count "$work")" -eq 2 ]
    printf '%s\n' "$expected" > "$work/expected"
    /bin/busybox cmp "$work/snapshot" "$work/expected"
    make_run "$work"
    [ "$(recipe_count "$work")" -eq 2 ]
}

case_run() {
    kind=$1
    root=$2
    work="$root/m82-make-$kind"
    case "$kind" in
      pipeline)
        prepare_pipeline "$work"
        make_run "$work"
        [ "$(recipe_count "$work")" -eq 2 ]
        assert_pipeline "$work" 'ALPHA one' ;;
      noop)
        prepare_pipeline "$work"
        make_run "$work"
        [ "$(recipe_count "$work")" -eq 2 ]
        make_run "$work"
        [ "$(recipe_count "$work")" -eq 2 ]
        assert_pipeline "$work" 'ALPHA one' ;;
      source-change)
        prepare_pipeline "$work"
        make_run "$work"
        [ "$(recipe_count "$work")" -eq 2 ]
        # Deliberately span one timestamp second on coarse as well as nano-
        # precise clocks. Never rely on 2026 wall time inside QEMU.
        /bin/busybox sleep 1
        printf 'beta two\n' > "$work/source.txt"
        make_run "$work"
        [ "$(recipe_count "$work")" -eq 4 ]
        assert_pipeline "$work" 'BETA two'
        make_run "$work"
        [ "$(recipe_count "$work")" -eq 4 ] ;;
      dir-create)
        prepare_directory "$work"
        stamp_directory_baseline "$work"
        printf 'b\n' > "$work/inputs/b"
        assert_directory_rebuilt "$work" 'a
b' ;;
      dir-remove)
        prepare_directory "$work" extra-file
        printf 'a\nb\n' > "$work/both"
        /bin/busybox cmp "$work/snapshot" "$work/both"
        stamp_directory_baseline "$work"
        /bin/busybox rm "$work/inputs/b"
        assert_directory_rebuilt "$work" 'a' ;;
      dir-rename)
        prepare_directory "$work"
        stamp_directory_baseline "$work"
        /bin/busybox mv "$work/inputs/a" "$work/inputs/z"
        assert_directory_rebuilt "$work" 'z' ;;
      dir-mkdir)
        prepare_directory "$work"
        stamp_directory_baseline "$work"
        /bin/busybox mkdir "$work/inputs/newdir"
        assert_directory_rebuilt "$work" 'a
newdir' ;;
      dir-rmdir)
        prepare_directory "$work" extra-dir
        printf 'a\nempty\n' > "$work/both"
        /bin/busybox cmp "$work/snapshot" "$work/both"
        stamp_directory_baseline "$work"
        /bin/busybox rmdir "$work/inputs/empty"
        assert_directory_rebuilt "$work" 'a' ;;
      cross-rename)
        prepare_directory "$work"
        /bin/busybox mkdir "$work/source-dir"
        printf 'z\n' > "$work/source-dir/z"
        stamp_directory_baseline "$work"
        /bin/busybox touch -t 197001010000.00 "$work/source-dir"
        source_before=$(/bin/busybox stat -c %y "$work/source-dir")
        /bin/busybox mv "$work/source-dir/z" "$work/inputs/z"
        [ "$(/bin/busybox stat -c %y "$work/source-dir")" != "$source_before" ]
        assert_directory_rebuilt "$work" 'a
z' ;;
      *) echo "[M82 UNKNOWN CASE] $kind" >&2; return 2 ;;
    esac
}

if [ "${1:-}" = --case ]; then
    [ "$#" -eq 3 ] || exit 2
    set -eu
    original_make
    case_run "$2" "$3"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m82-make-run.sh [--case kind provider-root]' >&2; exit 2; }
set -u

if ! original_make; then
    echo M82_ORIGINAL_MAKE_FAIL
    echo M82_MAKE_BATCH_FAIL
    exit 1
fi
echo M82_ORIGINAL_MAKE_PASS

failures=0
cases=0
for provider in TMPFS RAMFS EXT4; do
    case "$provider" in
      TMPFS) root=/tmp ;;
      RAMFS) root=/ramfs ;;
      EXT4) root=/mnt ;;
    esac
    provider_failures=0
    for kind in pipeline noop source-change dir-create dir-remove \
                dir-rename dir-mkdir dir-rmdir cross-rename; do
        label=$(printf '%s' "$kind" | /bin/busybox tr '[:lower:]-' '[:upper:]_')
        cases=$((cases + 1))
        if /bin/sh /tests/m82-make-run.sh --case "$kind" "$root"; then
            echo "M82_${provider}_${label}_PASS"
        else
            rc=$?
            echo "[M82 ${provider}/${kind} FAILED] actual GNU Make dependency or directory timestamp, rc=$rc" >&2
            echo "M82_${provider}_${label}_FAIL"
            failures=$((failures + 1))
            provider_failures=$((provider_failures + 1))
        fi
    done
    if [ "$provider_failures" -eq 0 ]; then
        echo "M82_${provider}_ALL9_PASS"
    else
        echo "M82_${provider}_ALL9_FAIL"
    fi
done

if [ "$failures" -ne 0 ] || [ "$cases" -ne 27 ]; then
    echo "[M82 FS MAKE FAIL] $failures failures across 27 original GNU Make and true VFS tests (observed=$cases)" >&2
    echo M82_MAKE_BATCH_FAIL
    exit 1
fi
printf 'NaSeed-M82-GNU-Make-original-3FS-27-PASS\n' > /mnt/m82-make-persisted.txt
/bin/busybox sync
echo M82_MAKE_BATCH_PASS
