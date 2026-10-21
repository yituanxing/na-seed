#!/bin/sh
# Deterministic uncompressed CPIO newc fixture containing REAL Alpine ELFs.
# The hostname is an archive file, not a syscall-specific kernel response.
set -eu
out="$1"
root=build/alpine-rootfs
rm -rf "$root"
mkdir -p "$root/bin" "$root/lib" "$root/etc" "$root/tmp"
cp build/alpine/bin/busybox "$root/bin/busybox"
cp build/alpine/lib/ld-musl-x86_64.so.1 "$root/lib/ld-musl-x86_64.so.1"
printf 'naseed\n' > "$root/etc/hostname"
chmod 755 "$root" "$root/bin" "$root/lib" "$root/etc" \
    "$root/bin/busybox" "$root/lib/ld-musl-x86_64.so.1"
chmod 644 "$root/etc/hostname"
chmod 1777 "$root/tmp" # mountpoint only; actual new files live in kernel RAM
# Genuine BusyBox applet symlinks, not duplicated executables or kernel
# pathname aliases. CPIO newc preserves links as S_IFLNK + target bytes.
applets='cat wc head yes sha256sum readlink ls grep mkdir rmdir rm cp mv sh'
for applet in $applets; do
    ln -s busybox "$root/bin/$applet"
done
touch -t 197001010000 "$root" "$root/bin" "$root/lib" "$root/etc" "$root/tmp" \
    "$root/bin/busybox" "$root/lib/ld-musl-x86_64.so.1" "$root/etc/hostname"
for applet in $applets; do
    touch -h -t 197001010000 "$root/bin/$applet"
done
# The optional second *authentic* ELF uses the pinned Debian BusyBox static
# binary already validated by the existing Debian QEMU regression.
if [ -n "${NASEED_SECOND_BUSYBOX:-}" ]; then
    test -f "$NASEED_SECOND_BUSYBOX"
    mkdir -p "$root/usr/bin"
    cp "$NASEED_SECOND_BUSYBOX" "$root/usr/bin/busybox"
    chmod 755 "$root/usr" "$root/usr/bin" "$root/usr/bin/busybox"
    touch -t 197001010000 "$root/usr" "$root/usr/bin" "$root/usr/bin/busybox"
fi
# Optional supplementary ABI executable: not a replacement for the pinned
# unmodified Alpine BusyBox that owns the normal acceptance matrix.
if [ -n "${NASEED_PATH_ABI:-}" ]; then
    test -f "$NASEED_PATH_ABI"
    cp "$NASEED_PATH_ABI" "$root/bin/path-abi"
    chmod 755 "$root/bin/path-abi"
    touch -t 197001010000 "$root/bin/path-abi"
fi
# Optional M8.0 native Linux shebang fixtures. The interpreter is the
# SAME pinned unmodified Alpine BusyBox ELF already present in /bin.
if [ -n "${NASEED_SHEBANG_ABI:-}" ]; then
    test -f "$NASEED_SHEBANG_ABI"
    cp "$NASEED_SHEBANG_ABI" "$root/bin/shebang-abi"
    cat >"$root/etc/naseed-stage.sh" <<'SCRIPT'
#!/bin/busybox sh
[ "$1" = token ] || exit 17
echo script-native:$1
echo hello > /tmp/from-script
cat /tmp/from-script | wc -c
echo script-success
SCRIPT
    cat >"$root/etc/naseed-normal.sh" <<'SCRIPT'
#!/bin/sh
[ "$1" = token ] || exit 18
echo normal-native:$1
SCRIPT
    cat >"$root/etc/naseed-flow.sh" <<'SCRIPT'
#!/bin/sh
mkdir /tmp/work
echo source > /tmp/work/in
cp /tmp/work/in /tmp/work/out
cat /tmp/work/out | wc -c
mv /tmp/work/out /tmp/work/result
cat /tmp/work/result
rm /tmp/work/in
rm /tmp/work/result
rmdir /tmp/work
echo flow-success
SCRIPT
    cat >"$root/etc/naseed-missing.sh" <<'SCRIPT'
#!/bin/does-not-exist sh
echo wrong
SCRIPT
    cat >"$root/etc/naseed-plain.sh" <<'SCRIPT'
echo without-shebang
SCRIPT
    cat >"$root/etc/naseed-noexec.sh" <<'SCRIPT'
#!/bin/busybox sh
echo wrong
SCRIPT
    chmod 755 "$root/bin/shebang-abi" "$root/etc/naseed-stage.sh" \
        "$root/etc/naseed-normal.sh" "$root/etc/naseed-flow.sh" \
        "$root/etc/naseed-missing.sh" "$root/etc/naseed-plain.sh"
    chmod 644 "$root/etc/naseed-noexec.sh"
    touch -t 197001010000 "$root/bin/shebang-abi" \
        "$root/etc/naseed-stage.sh" "$root/etc/naseed-normal.sh" \
        "$root/etc/naseed-flow.sh" "$root/etc/naseed-missing.sh" \
        "$root/etc/naseed-plain.sh" "$root/etc/naseed-noexec.sh"
fi
mkdir -p "$(dirname "$out")"
(
    cd "$root"
    {
        printf '%s\n' . bin bin/busybox etc etc/hostname lib lib/ld-musl-x86_64.so.1 tmp
        for applet in $applets; do printf '%s\n' "bin/$applet"; done
        if [ -n "${NASEED_SECOND_BUSYBOX:-}" ]; then
            printf '%s\n' usr usr/bin usr/bin/busybox
        fi
        if [ -n "${NASEED_PATH_ABI:-}" ]; then
            printf '%s\n' bin/path-abi
        fi
        if [ -n "${NASEED_SHEBANG_ABI:-}" ]; then
            printf '%s\n' bin/shebang-abi \
                etc/naseed-stage.sh etc/naseed-normal.sh \
                etc/naseed-flow.sh etc/naseed-missing.sh \
                etc/naseed-plain.sh etc/naseed-noexec.sh
        fi
    } | cpio -o -H newc --quiet --owner=0:0
) > "$out.tmp"
mv "$out.tmp" "$out"
echo '[CPIO] built archive of actual Alpine ELF + fixed hostname fixture'
