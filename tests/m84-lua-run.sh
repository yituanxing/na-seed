#!/bin/sh
# M84: unchanged upstream Lua 5.4.8, built INSIDE NaSeed on writable ext4
# by the original signed Alpine GCC and GNU Make. This script owns only the
# guest software workload; the existing APK installer and QEMU runners own
# provenance, block devices, and the two real machine boots.
set -eu
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME LC_ALL LANG

fail() { echo "[M84 LUA FAIL] $*" >&2; exit 1; }
src=/mnt/m84-lua-5.4.8
lua="$src/src/lua"
luac="$src/src/luac"

mounted_rw() {
    path=$1 fs=$2
    while read -r dev target kind mode rest; do
        [ "$target" = "$path" ] || continue
        [ "$kind" = "$fs" ] && [ "$mode" = rw ]
        return $?
    done < /proc/mounts
    return 1
}

verify_environment() {
    mounted_rw /mnt lwext4 || fail 'physical VirtIO ext4 is not writable'
    mounted_rw /tmp tmpfs || fail 'tmpfs is not writable'
    mounted_rw /ramfs upstream-ramfs || fail 'original RamFS is not writable'
    for path in /usr/bin/gcc /usr/bin/make /usr/bin/ar /usr/bin/ranlib \
                /usr/bin/readelf /usr/lib/libc.a; do
        [ -s "$path" ] || fail "missing authenticated native tool/sysroot $path"
    done
    /usr/bin/gcc --version | /bin/busybox grep -q gcc || fail 'not original GCC'
    /usr/bin/make --version | /bin/busybox grep -q '^GNU Make ' ||
        fail 'not original GNU Make'
    [ -s /tests/m84-lua-vm.lua ] && [ -s /tests/m84-lua-io.lua ] ||
        fail 'missing unchanged real Lua VM and IO test scripts'
    echo M84_LUA_SIGNED_GCC_MAKE_PASS
}

make_query() {
    # The top-level `posix` recipe is phony. Query the actual upstream src
    # `lua` ELF target to distinguish 0=no work, 1=dirty, 2=Make error.
    ( cd "$src/src" && /usr/bin/make --no-print-directory -q lua \
        CC='/usr/bin/gcc -std=gnu99' AR='/usr/bin/ar rcu' \
        RANLIB=/usr/bin/ranlib MYLDFLAGS='-static -no-pie' \
        MYLIBS=-ldl SYSCFLAGS=-DLUA_USE_POSIX )
}

assert_make_clean() {
    make_query || fail 'upstream Lua target was not a genuine Make no-op'
}

assert_make_dirty() {
    if make_query; then
        fail 'upstream Lua Make did not detect changed lvm.c'
    else
        rc=$?
        [ "$rc" -eq 1 ] || fail "upstream Lua Make -q returned error $rc"
    fi
}

run_lua_tests() {
    executable=$1 label=$2
    vm=$("$executable" /tests/m84-lua-vm.lua) ||
        fail "$label original Lua VM/GC/coroutines failed"
    [ "$vm" = lua-vm-ok ] || fail "$label wrong Lua VM output: $vm"
    io=$("$executable" /tests/m84-lua-io.lua) ||
        fail "$label original Lua stdio/filesystem failed"
    [ "$io" = lua-io-ok ] || fail "$label wrong Lua IO output: $io"
}

verify_built_elf() {
    [ -s "$lua" ] && [ -x "$lua" ] && [ -s "$luac" ] ||
        fail 'upstream Make did not produce real Lua/luac ELF binaries'
    /usr/bin/readelf -h "$lua" | /bin/busybox grep -q ELF ||
        fail 'upstream compiler did not produce ELF'
    # `MYLDFLAGS=-static -no-pie` must not quietly produce an interpreter-
    # dependent dynamic binary that only happens to execute in this rootfs.
    if /usr/bin/readelf -l "$lua" | /bin/busybox grep -q INTERP; then
        fail 'upstream Lua ELF unexpectedly requires a dynamic interpreter'
    fi
    version=$("$lua" -v 2>&1) || fail 'guest-built Lua -v failed'
    printf '%s\n' "$version" | /bin/busybox grep -Fq 'Lua 5.4.8' ||
        fail "guest-built Lua release mismatch: $version"
}

build_mode() {
    verify_environment
    [ -s /tests/m84-lua.tar.gz ] || fail 'missing verified upstream tarball'
    [ ! -e "$src" ] && [ ! -e /mnt/lua-5.4.8 ] ||
        fail 'unexpected existing Lua source tree on fresh disk'
    # No tar -P, no source patches, no reconstructed Makefiles; the archive
    # has already been independently SHA256 verified on the Linux host.
    /bin/busybox tar -mxzf /tests/m84-lua.tar.gz -C /mnt ||
        fail 'original Lua upstream archive extraction'
    /bin/busybox mv /mnt/lua-5.4.8 "$src" ||
        fail 'rename original source tree under persistent ext4'
    [ -s "$src/src/lvm.c" ] && [ -s "$src/src/Makefile" ] &&
        [ -s "$src/Makefile" ] || fail 'original Lua source/Makefiles absent'
    echo M84_LUA_ORIGINAL_SOURCE_PASS

    ( cd "$src" && /usr/bin/make --no-print-directory -j2 posix \
        CC='/usr/bin/gcc -std=gnu99' AR='/usr/bin/ar rcu' \
        RANLIB=/usr/bin/ranlib MYLDFLAGS='-static -no-pie' MYLIBS=-ldl ) \
        > /tmp/m84-lua-build.log 2>&1 || {
        /bin/busybox tail -n 100 /tmp/m84-lua-build.log >&2
        fail 'real original upstream Lua GNU Make/GCC build'
    }
    verify_built_elf
    echo M84_LUA_NATIVE_BUILD_PASS
    run_lua_tests "$lua" ext4
    echo M84_LUA_VM_GC_COROUTINE_AND_IO_PASS

    assert_make_clean
    echo M84_LUA_MAKE_NOOP_PASS
    before_vm=$(/bin/busybox stat -c %Y "$src/src/lvm.o")
    before_api=$(/bin/busybox stat -c %y "$src/src/lapi.o")
    /bin/busybox sleep 1
    printf '\n/* M84: source-working-copy selective Make rebuild test. */\n' \
        >> "$src/src/lvm.c"
    assert_make_dirty
    ( cd "$src/src" && /usr/bin/make --no-print-directory -j2 lua \
        CC='/usr/bin/gcc -std=gnu99' AR='/usr/bin/ar rcu' \
        RANLIB=/usr/bin/ranlib MYLDFLAGS='-static -no-pie' MYLIBS=-ldl \
        SYSCFLAGS=-DLUA_USE_POSIX ) > /tmp/m84-lua-incremental.log 2>&1 || {
        /bin/busybox tail -n 80 /tmp/m84-lua-incremental.log >&2
        fail 'real Lua incremental recompile/link failed'
    }
    after_vm=$(/bin/busybox stat -c %Y "$src/src/lvm.o")
    after_api=$(/bin/busybox stat -c %y "$src/src/lapi.o")
    [ "$after_vm" -gt "$before_vm" ] && [ "$after_api" = "$before_api" ] ||
        fail 'upstream Lua rebuilt unaffected object or skipped changed VM'
    assert_make_clean
    run_lua_tests "$lua" ext4-rebuilt
    echo M84_LUA_SELECTIVE_REBUILD_PASS

    # Execute the *same* guest-compiled original ELF on two different VFS
    # providers, never substitute host-built or packaged Lua binaries.
    for fs in /tmp /ramfs; do
        copy="$fs/m84-lua"
        /bin/busybox cp "$lua" "$copy" || fail "copy Lua ELF to $fs"
        /bin/busybox chmod 755 "$copy" || fail "chmod Lua ELF on $fs"
        run_lua_tests "$copy" "$fs"
    done
    echo M84_LUA_CROSS_FS_EXEC_PASS

    printf 'NaSeed-M84-original-Lua-5.4.8-native-GCC-PASS\n' \
        > /mnt/m84-lua-result.txt || fail 'write ext4 Lua persistence record'
    /bin/busybox sync || fail 'flush compiled Lua ELF and result'
    echo M84_LUA_BUILD_PASS
}

replay_mode() {
    verify_environment
    [ -s "$src/src/Makefile" ] || fail 'source tree missing after real reboot'
    verify_built_elf
    run_lua_tests "$lua" replay
    echo M84_LUA_REPLAY_EXEC_PASS
    assert_make_clean
    echo M84_LUA_REPLAY_NOOP_PASS
    persisted=$(/bin/busybox cat /mnt/m84-lua-result.txt) ||
        fail 'ext4 Lua completion record missing after reboot'
    [ "$persisted" = 'NaSeed-M84-original-Lua-5.4.8-native-GCC-PASS' ] ||
        fail 'ext4 Lua completion record changed'
    echo M84_LUA_REPLAY_PASS
}

case "${1:-}" in
    --build) [ "$#" -eq 1 ] || exit 2; build_mode ;;
    --replay) [ "$#" -eq 1 ] || exit 2; replay_mode ;;
    *) echo 'usage: m84-lua-run.sh --build|--replay' >&2; exit 2 ;;
esac
