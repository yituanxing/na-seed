#!/bin/sh
# M83: original upstream zlib's own ./configure + Makefile, compiled by
# signed native Alpine GCC/GNU Make on a real VirtIO-backed ext4 filesystem.
# The very same guest-built ELF then executes from independent tmpfs and the
# unmodified third-party RamFS. A second REAL QEMU boot replays the ext4 ELF.
# Upstream source bytes and Makefile are never patched or replaced.
set -eu
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME LC_ALL LANG

fail() {
    echo "[M83 FAIL] $*" >&2
    exit 1
}

mounted_rw() {
    expected_path=$1 expected_fs=$2
    [ -r /proc/mounts ] || return 1
    while read -r source target fs mode rest; do
        [ "$target" = "$expected_path" ] || continue
        [ "$fs" = "$expected_fs" ] && [ "$mode" = rw ]
        return $?
    done < /proc/mounts
    return 1
}

verify_mounts() {
    mounted_rw /tmp tmpfs || fail 'tmpfs not mounted writable at /tmp'
    mounted_rw /ramfs upstream-ramfs || fail 'original RamFS not mounted writable'
    mounted_rw /mnt lwext4 || fail 'VirtIO ext4 not mounted writable'
}

verify_native_tools() {
    for command in /usr/bin/gcc /usr/bin/make /usr/bin/ar \
                   /usr/bin/ld /usr/bin/readelf; do
        [ -x "$command" ] || fail "missing original native tool $command"
    done
    /usr/bin/gcc --version | /bin/busybox grep -q 'gcc' || fail 'not original GCC'
    /usr/bin/make --version | /bin/busybox grep -q '^GNU Make ' || fail 'not GNU Make'
    test -s /usr/lib/libc.a || fail 'missing signed Alpine musl sysroot'
    test -s /tests/m83-zlib.tar || fail 'missing pinned upstream Git archive'
    echo M83_SIGNED_GCC_MAKE_PASS
}

assert_make_clean() {
    project=$1 target=$2
    ( cd "$project" && /usr/bin/make --no-print-directory -q "$target" ) ||
        fail "Make did not report clean target $target"
}

assert_make_dirty() {
    project=$1 target=$2
    if ( cd "$project" && /usr/bin/make --no-print-directory -q "$target" ); then
        fail "Make did not notice changed dependency of $target"
    else
        rc=$?
        [ "$rc" -eq 1 ] || fail "Make query errored rc=$rc for $target"
    fi
}

run_zlib_example() {
    dir=$1 executable=$2 output=$3
    ( cd "$dir" && "$executable" ) > "$output" ||
        fail "guest-built upstream zlib example failed from $dir"
    /bin/busybox grep -Fq 'zlib version 1.3.1' "$output" ||
        fail "wrong zlib version from $dir"
    /bin/busybox grep -Fq 'hello, hello!' "$output" ||
        fail "upstream zlib compression oracle failed from $dir"
}

# The existing M54 two-translation-unit C project is reused unchanged. It
# checks real GCC objects, linker/ELF exec, Make's no-op vs incremental rebuild
# on both volatile backends; it does not demand hardlinks or rename-overwrite
# from upstream RamFS, which intentionally does not implement those.
two_tu_project() {
    root=$1 label=$2
    project="$root/m83-two-tu"
    /bin/busybox mkdir -p "$project"
    /bin/busybox cp /tests/m83-main.c "$project/main.c"
    /bin/busybox cp /tests/m83-answer.c "$project/answer.c"
    /bin/busybox cp /tests/m83-answer.h "$project/answer.h"
    /bin/busybox cp /tests/m83-Makefile "$project/Makefile"
    ( cd "$project" && /usr/bin/make --no-print-directory -j2 all ) ||
        fail "native GCC/Make initial $label two-TU compile"
    [ -s "$project/main.o" ] && [ -s "$project/answer.o" ] &&
        [ -s "$project/app" ] || fail "missing native $label build outputs"
    [ "$(cd "$project" && ./app)" = 'M54-APP answer=1' ] ||
        fail "native $label linked app first execution"
    assert_make_clean "$project" app
    before_main=$(/bin/busybox stat -c %Y "$project/main.o")
    before_answer=$(/bin/busybox stat -c %Y "$project/answer.o")
    /bin/busybox sleep 1
    /bin/sed 's/return 1;/return 7;/' "$project/answer.c" > "$project/answer.new"
    /bin/busybox cp "$project/answer.new" "$project/answer.c"
    /bin/busybox rm "$project/answer.new"
    assert_make_dirty "$project" app
    ( cd "$project" && /usr/bin/make --no-print-directory -j2 all ) ||
        fail "native $label selective rebuild failed"
    after_main=$(/bin/busybox stat -c %Y "$project/main.o")
    after_answer=$(/bin/busybox stat -c %Y "$project/answer.o")
    [ "$before_main" = "$after_main" ] &&
        [ "$after_answer" -gt "$before_answer" ] ||
        fail "native $label rebuilt unrelated object or skipped changed one"
    [ "$(cd "$project" && ./app)" = 'M54-APP answer=7' ] ||
        fail "native $label linked app did not reflect updated source"
    assert_make_clean "$project" app
    echo "M83_${label}_NATIVE_TWO_TU_PASS"
}

build_original_zlib() {
    src=/mnt/m83-upstream-zlib
    /bin/busybox mkdir -p "$src"
    # `-m` deliberately sets extraction-time mtimes. With a missing RTC,
    # original Git source timestamps from 2024 otherwise lie in the guest's
    # future, making Make rebuild EVERY object after a successful compile.
    # This changes no source bytes, is a normal GNU/BusyBox tar option and
    # keeps time-of-build ordering on each supported guest timekeeper.
    /bin/busybox tar -mxf /tests/m83-zlib.tar -C "$src" ||
        fail 'extracting verified unchanged upstream Git archive'
    test -x "$src/configure" && test -s "$src/Makefile.in" &&
        test -s "$src/deflate.c" && test -s "$src/test/example.c" ||
        fail 'original upstream source/configure/Makefile missing'
    (
        cd "$src"
        CC=/usr/bin/gcc CFLAGS=-O2 ./configure --static
    ) > /tmp/m83-configure.log 2>&1 || {
        /bin/busybox tail -n 65 /tmp/m83-configure.log >&2
        fail 'original zlib ./configure --static'
    }
    test -s "$src/Makefile" && test -s "$src/zconf.h" ||
        fail 'original configure did not produce its genuine Makefile'
    echo M83_EXT4_ZLIB_CONFIGURE_PASS

    ( cd "$src" && /usr/bin/make --no-print-directory -j2 all ) ||
        fail 'original zlib GNU Make/GCC build'
    test -s "$src/libz.a" && test -x "$src/example" &&
        test -x "$src/minigzip" && test -s "$src/deflate.o" ||
        fail 'original GCC build missing library, executable or objects'
    /usr/bin/readelf -h "$src/example" | /bin/busybox grep -q 'ELF' ||
        fail 'guest-compiled example is not authentic ELF'
    echo M83_EXT4_ZLIB_GCC_BUILD_PASS

    ( cd "$src" && /usr/bin/make --no-print-directory teststatic ) \
        > /tmp/m83-upstream-test.log 2>&1 || {
        /bin/busybox tail -n 80 /tmp/m83-upstream-test.log >&2
        fail 'original upstream zlib make test failed'
    }
    /bin/busybox grep -Fq 'zlib test OK' /tmp/m83-upstream-test.log ||
        fail 'original upstream zlib did not print functional test PASS'
    echo M83_EXT4_ZLIB_UPSTREAM_TEST_PASS

    assert_make_clean "$src" libz.a
    echo M83_EXT4_ZLIB_INCREMENTAL_NOOP_PASS
    before_deflate=$(/bin/busybox stat -c %Y "$src/deflate.o")
    before_crc=$(/bin/busybox stat -c %y "$src/crc32.o")
    /bin/busybox sleep 1
    printf '\n/* M83: intentionally modify only the writable upstream source copy. */\n' \
        >> "$src/deflate.c"
    assert_make_dirty "$src" libz.a
    ( cd "$src" && /usr/bin/make --no-print-directory -j2 all ) ||
        fail 'original zlib dependent recompile/link failed'
    after_deflate=$(/bin/busybox stat -c %Y "$src/deflate.o")
    after_crc=$(/bin/busybox stat -c %y "$src/crc32.o")
    [ "$after_deflate" -gt "$before_deflate" ] &&
        [ "$after_crc" = "$before_crc" ] ||
        fail 'original Make rebuilt unrelated CRC object or skipped deflate'
    assert_make_clean "$src" libz.a
    echo M83_EXT4_ZLIB_SELECTIVE_REBUILD_PASS

    run_zlib_example "$src" ./example /tmp/m83-ext4-example.out
    ( cd "$src" && /usr/bin/make --no-print-directory teststatic ) \
        > /tmp/m83-retest.log 2>&1 || {
        /bin/busybox tail -n 80 /tmp/m83-retest.log >&2
        fail 'original zlib full retest after selective rebuild'
    }
    /bin/busybox grep -Fq 'zlib test OK' /tmp/m83-retest.log ||
        fail 'original zlib retest did not pass'
    echo M83_EXT4_ZLIB_EXEC_PASS
}

copy_exec_to_volatile() {
    for location in /tmp /ramfs; do
        /bin/busybox cp /mnt/m83-upstream-zlib/example "$location/m83-zlib-example"
        /bin/busybox chmod 755 "$location/m83-zlib-example"
        run_zlib_example "$location" ./m83-zlib-example \
            /tmp/m83-$(/bin/busybox basename "$location")-example.out
    done
    echo M83_CROSS_FS_EXEC_PASS
}

build_mode() {
    verify_mounts
    verify_native_tools
    build_original_zlib
    two_tu_project /tmp TMPFS
    two_tu_project /ramfs RAMFS
    copy_exec_to_volatile
    # Host debugfs reads this signature and independently dumps the actual
    # compiled ELF and libz.a from a cleanly unmounted physical ext4 disk.
    if ! printf 'NaSeed-M83-upstream-zlib-1.3.1-GCC-Make-PASS\n' \
            > /mnt/m83-result.txt || ! /bin/busybox sync; then
        fail 'ext4 build persistence record write/sync failed'
    fi
    echo M83_EXT4_PERSISTENCE_PASS
    echo M83_BUILD_PASS
}

replay_mode() {
    verify_mounts
    verify_native_tools
    src=/mnt/m83-upstream-zlib
    test -s "$src/libz.a" && test -x "$src/example" ||
        fail 'guest-built ext4 library and ELF not present after actual reboot'
    run_zlib_example "$src" ./example /tmp/m83-replay-example.out
    echo M83_REPLAY_ORIGINAL_ZLIB_PASS
    assert_make_clean "$src" libz.a
    echo M83_REPLAY_INCREMENTAL_NOOP_PASS
    [ "$(/bin/busybox cat /mnt/m83-result.txt)" = \
      'NaSeed-M83-upstream-zlib-1.3.1-GCC-Make-PASS' ] ||
        fail 'persistent original-source completion record corrupted'
    echo M83_REPLAY_PERSISTENCE_PASS
    echo M83_REPLAY_PASS
}

case "${1:-}" in
    --build) [ "$#" -eq 1 ] || exit 2; build_mode ;;
    --replay) [ "$#" -eq 1 ] || exit 2; replay_mode ;;
    *) echo 'usage: m83-source-run.sh --build|--replay' >&2; exit 2 ;;
esac
