#!/bin/sh
# Genuine pinned upstream BusyBox Kbuild IN a running NaSeed Linux-ABI guest.
# No prebuilt BusyBox substituted after boot. Guest tools are original Alpine.
set -eu
stage=init
trap 'rc=$?; if [ "$rc" -ne 0 ]; then echo "[FAIL] M56 stage=$stage rc=$rc" >&2; fi' EXIT
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
KCONFIG_NOTIMESTAMP=1
export PATH HOME LC_ALL LANG KCONFIG_NOTIMESTAMP
original=/tests/m56-source
src=/tmp/m56-source
test -s "$original/Makefile"
test ! -e "$original/busybox"
test ! -e "$original/.config"
for tool in /usr/bin/gcc /usr/bin/make /usr/bin/as /usr/bin/ld /usr/bin/readelf; do
    test -x "$tool"
done
echo M56_GENUINE_NATIVE_ALPINE_TOOLCHAIN_PASS

# True guest execve regression: previous 1024-byte fixed argument buffer
# rejected ordinary upstream Make shell commands. Do not fake shell output.
stage=linux-execve-long-shell-argument
long=$(/bin/busybox printf '%02048d' 0)
/bin/sh -c 'test "${#1}" -eq 2048' m56-argv "$long"
echo M56_LINUX_LONG_EXECVE_ARG_PASS

stage=copy-pinned-upstream-to-writable-tmpfs
# Immutable bootfs cannot be modified by GNU Make. In the guest, copy the
# genuine source tree into writable tmpfs, including its directory layout.
# Do not replace sources, generated scripts or Kconfig.
test ! -e "$src"
/bin/busybox cp -R "$original" "$src"
test -s "$src/Makefile"
test -s "$src/Config.in"
echo M56_UPSTREAM_SOURCE_IN_TMPFS_PASS

stage=original-guest-kconfig
cd "$src"
/usr/bin/make HOSTCC=/usr/bin/gcc CC=/usr/bin/gcc allnoconfig
test -s .config
# Reuse *exact* same verified feature set as host source-BusyBox oracle;
# keep all config resolution and HOSTCC helpers native INSIDE NaSeed.
for pass in 1 2 3; do
    while IFS= read -r setting || [ -n "$setting" ]; do
        case "$setting" in ''|\#*) continue ;; esac
        key=${setting%%=*}
        if /bin/busybox grep -Fxq "$setting" .config; then continue; fi
        if /bin/busybox grep -Fqx "# $key is not set" .config; then
            /bin/busybox sed -i "s|^# $key is not set$|$setting|" .config
        elif /bin/busybox grep -q "^$key=" .config; then
            /bin/busybox sed -i "s|^$key=.*$|$setting|" .config
        fi
    done < /tests/m56.config
    /bin/busybox yes '' | /usr/bin/make HOSTCC=/usr/bin/gcc CC=/usr/bin/gcc oldconfig
done
while IFS= read -r setting || [ -n "$setting" ]; do
    case "$setting" in ''|\#*) continue ;; esac
    /bin/busybox grep -Fxq "$setting" .config || {
        echo "[M56 FAIL] Kconfig rejected pinned feature: $setting"
        exit 1
    }
done < /tests/m56.config
echo M56_NATIVE_KCONFIG_PINNED_OPTIONS_PASS

stage=real-native-gnu-make-gcc-link
# Use real in-guest GNU Make, Alpine GCC/AS/LD, musl and upstream Kbuild.
# In-tree build avoids depending on a tmpfs symlink feature for O=.
# No fabricated build logs or precompiled objects.
echo M56_REAL_UPSTREAM_KBUILD_BEGIN
/usr/bin/make -j2 HOSTCC=/usr/bin/gcc CC=/usr/bin/gcc busybox
test -s "$src/busybox"
test -x "$src/busybox"
/usr/bin/readelf -h "$src/busybox" | /bin/busybox grep -Fq 'EXEC (Executable file)'
if /usr/bin/readelf -l "$src/busybox" | /bin/busybox grep -Fq INTERP; then
    echo '[M56 FAIL] pinned static BusyBox unexpectedly requires a dynamic interpreter'
    exit 1
fi
echo M56_REAL_UPSTREAM_KBUILD_LINK_PASS

stage=guest-built-original-busybox-applet-tests
test "$("$src/busybox" echo guest-native-ok)" = guest-native-ok
"$src/busybox" --list > /tmp/m56-applet-list
for applet in sh echo cat wc ls mkdir rm rmdir; do
    /bin/busybox grep -Fxq "$applet" /tmp/m56-applet-list
done
test "$("$src/busybox" sh -c 'echo inside-self-built-ash')" = inside-self-built-ash
/bin/busybox printf 'aa\nbb\n' > /tmp/m56-lines.txt
test "$("$src/busybox" wc -l < /tmp/m56-lines.txt | /bin/busybox tr -d ' ')" = 2
"$src/busybox" mkdir /tmp/m56-made-dir
"$src/busybox" rm -r /tmp/m56-made-dir
test ! -e /tmp/m56-made-dir
echo M56_NEW_GUEST_BUILT_BUSYBOX_EXEC_PASS
echo M56_REAL_GUEST_BUSYBOX_FULL_PASS
