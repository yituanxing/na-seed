#!/bin/sh
# Real original Alpine PID1 on physical ext4: direct public HTTPS one-transaction
# install of Bash/Lua/SQLite/Git, integrated work and independent second QEMU.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: sh scripts/probe-m197-alpine-software-cohort.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in
    amd64) alpine_arch=x86_64 ;;
    riscv64) alpine_arch=riscv64 ;;
    arm64) alpine_arch=aarch64 ;;
    *) exit 2 ;;
esac
. scripts/native-qemu-target.sh

# M198 reuses THIS full public-Alpine workload and the same disposable disk.
# It adds already-established VirtIO GPU/input + userspace desktop, without
# another distro image, PID1, graphical kernel ABI or software-test pipeline.
m198=${NAS_M198_INTEGRATED_DESKTOP:-0}
case "$m198" in 0|1) ;; *) echo '[M198 FAIL] invalid desktop selector' >&2; exit 2 ;; esac
m199=${NAS_M199_ORIGINAL_OPENRC_DESKTOP:-0}
case "$m199" in 0|1) ;; *) echo '[M199 FAIL] invalid original OpenRC selector' >&2; exit 2 ;; esac
if [ "$m199" = 1 ]; then
    # OpenRC runs the exact M198 graphics and original Alpine software; only
    # its lifecycle and *second genuine Alpine PID1 boot* differ.
    m198=1
fi
if [ "$m198" = 1 ]; then
    # Must be set BEFORE M191 builds the native kernel on all three ISAs.
    export NAS_ENABLE_VIRTIO_INPUT=1 NAS_ENABLE_VIRTIO_GPU=1
    export NAS_M191_BUILD_TAG=m198-root
fi

export NAS_ENABLE_VIRTIO_NET=1 NAS_VIRTIO_NET_USER=1 NAS_ENABLE_LWIP=1
unset NAS_SYSCALL_TRACE
sh scripts/probe-m191-alpine-root.sh "$port"
work="build/external/m191-alpine-rootfs/$port"
root="$PWD/$work/rootfs"
baseline="$PWD/$work/alpine-root.img"
rescue="$PWD/$work/smoke/rescue/rootfs.cpio"
busybox="$PWD/$work/smoke/elf/busybox"
test -s "$baseline" && test -s "$rescue" && test -s "$busybox"

# An external mirror failure is a clear CI infrastructure failure, never a
# guest success or a reason to silently substitute the local M195 repository.
public_index="https://dl-cdn.alpinelinux.org/alpine/v3.23/main/$alpine_arch/APKINDEX.tar.gz"
if ! curl --noproxy '*' -fIsS --retry 2 --retry-delay 2 \
          --connect-timeout 12 --max-time 40 "$public_index" >/dev/null; then
    echo '[M197 PUBLIC INFRA FAIL] GitHub runner cannot reach official Alpine HTTPS CDN' >&2
    exit 1
fi

overlay=$(mktemp -d "$PWD/$work/m197-software-overlay.XXXXXX")
sudo cp -a "$root/." "$overlay/"
sudo python3 scripts/m193-alpine-login-overlay.py "$root" "$overlay"
sudo python3 scripts/m197-alpine-stage.py "$root" "$overlay"
if [ "$m198" = 1 ]; then
    sh scripts/stage-m198-alpine-desktop.sh "$port" "$overlay"
    if [ "$m199" = 1 ]; then
        sh scripts/stage-m199-alpine-openrc.sh "$port" "$overlay"
    fi
fi
sudo install -m 0644 tests/m197-software-workload.sh \
    "$overlay/root/m197-software-workload.sh"
sudo test -s "$overlay/root/m197-software-workload.sh"
sudo test ! -e "$overlay/root/m197-app" || {
    echo '[M197 PUBLIC STAGE FAIL] application output was preseeded' >&2; exit 1;
}
sudo test ! -e "$overlay/root/m197-wan.txt" || {
    echo '[M197 PUBLIC STAGE FAIL] persistence witness was preseeded' >&2; exit 1;
}
disk="$PWD/$work/m197-software-disposable.img"
sudo python3 scripts/m191-alpine-root-disk.py prepare "$port" "$overlay" "$disk"
sudo chown "$(id -u):$(id -g)" "$disk"

export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_M191_ALPINE_ROOT=1
export NAS_ENABLE_RAMFS=0 NAS_ENABLE_LITTLEFS=0
unset NAS_ENABLE_M179_FS_IRQ NAS_ENABLE_M180_FS_USER_IRQ
unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
export NAS_VIRTIO_BLK_IMAGE="$disk" NAS_ACCEPTANCE_CPIO="$rescue"
pcap="$PWD/$work/m197-public-wire.pcap"
wire_json="$PWD/$work/m197-public-wire.json"
test ! -e "$pcap" || { echo '[M197 PUBLIC WIRE FAIL] stale PCAP'; exit 1; }
export NAS_VIRTIO_NET_PCAP="$pcap"
export BUSYBOX_PARITY_BINARY="$busybox"
export BUSYBOX_PARITY_CASE=m197-batch BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m197-alpine-cases.tsv
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M197_PUBLIC_QEMU_TIMEOUT:-1020}
if [ "$m198" = 1 ]; then
    export NAS_QEMU_INPUT_OPTIN=1 NAS_QEMU_VIRTIO_GPU=1 NAS_QEMU_RAMFB=0
    export NAS_ACCEPTANCE_QEMU_RAM=2048M
    # One boot runs M197's full public transaction before GUI/Vim. Keep the
    # slow external WAN budget separate from the post-install QMP budget.
    if [ "$m199" = 1 ]; then
        export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M199_FIRST_BOOT_SECONDS:-2600}
    else
        export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M198_FIRST_BOOT_SECONDS:-2400}
    fi
    export NAS_QEMU_QMP_SOCKET="$PWD/$work/m198-integrated-desktop.qmp"
    test ! -e "$NAS_QEMU_QMP_SOCKET" || {
        echo '[M198 QMP FAIL] stale socket from another run' >&2
        exit 1
    }
fi
if [ "$port" = amd64 ]; then
    export NAS_ACCEPTANCE_KERNEL="$PWD/build/${NAS_M191_BUILD_TAG:-m191-root}-virtio-x86/kernel.elf"
    test -s "$NAS_ACCEPTANCE_KERNEL"
fi

if [ "$m199" = 1 ]; then
    # This is the SAME original /sbin/init manifest on BOTH boots; M198's
    # independent second QEMU previously used a CPIO BusyBox entry instead.
    export BUSYBOX_PARITY_MANIFEST=scripts/m199-alpine-boot-cases.tsv
fi

fifo="$PWD/$work/m197-software-serial-input.fifo"
export NAS_QEMU_SERIAL_FIFO="$fifo"
serial="$PWD/$logs/m197-batch.serial.log"
if [ "$m198" = 1 ]; then
    if [ "$m199" = 1 ]; then
        export NAS_M199_QMP_FAIL_FILE="$PWD/$work/m199-qmp-first.failed"
        export NAS_M199_QMP_PASS_FILE="$PWD/$work/m199-qmp-first.pass"
        test ! -e "$NAS_M199_QMP_FAIL_FILE" && test ! -e "$NAS_M199_QMP_PASS_FILE"
        python3 scripts/m199-qmp-alpine-openrc.py --mode first \
            --serial "$serial" --qmp "$NAS_QEMU_QMP_SOCKET" \
            --outdir "$work" --success-file "$NAS_M199_QMP_PASS_FILE" \
            --timeout "${NAS_M199_DESKTOP_SECONDS:-360}" \
            > "$work/m199-first-qmp.log" 2>&1 &
        watcher=$!
        if ! python3 scripts/m199-alpine-uart.py first "$runner" "$serial" \
                "$PWD/$work/m199-first-runner.log" "$fifo"; then
            kill "$watcher" 2>/dev/null || :
            wait "$watcher" 2>/dev/null || :
            tail -100 "$work/m199-first-qmp.log" >&2 || :
            exit 1
        fi
        if ! wait "$watcher"; then
            echo '[M199 FIRST QMP FAIL] physical Vim and OpenRC desktop' >&2
            tail -100 "$work/m199-first-qmp.log" >&2
            exit 1
        fi
        test -s "$NAS_M199_QMP_PASS_FILE"
    else
    export NAS_M198_QMP_FAIL_FILE="$PWD/$work/m198-qmp.failed"
    test ! -e "$NAS_M198_QMP_FAIL_FILE" || {
        echo '[M198 QMP FAIL] stale failure file' >&2; exit 1;
    }
    python3 scripts/m198-qmp-alpine-desktop.py \
        --serial "$serial" --qmp "$NAS_QEMU_QMP_SOCKET" \
        --outdir "$work" --timeout "${NAS_M198_DESKTOP_SECONDS:-300}" \
        > "$work/m198-qmp-check.log" 2>&1 &
    watcher=$!
    if ! python3 scripts/m198-alpine-uart.py "$runner" "$serial" \
            "$PWD/$work/m198-first-runner.log" "$fifo"; then
        kill "$watcher" 2>/dev/null || :
        wait "$watcher" 2>/dev/null || :
        tail -100 "$work/m198-qmp-check.log" >&2 || :
        exit 1
    fi
    if ! wait "$watcher"; then
        echo '[M198 QMP FAIL] physical desktop and original editor' >&2
        tail -100 "$work/m198-qmp-check.log" >&2
        exit 1
    fi
    grep -Fq '[M198 PHYSICAL QMP PASS]' "$work/m198-qmp-check.log"
    fi
else
    python3 scripts/m197-alpine-uart.py "$runner" "$serial" \
        "$PWD/$work/m197-first-runner.log" "$fifo"
fi
test -s "$serial" && test -s "$pcap"
cp "$serial" "$PWD/$work/m197-live.serial.log"
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$serial" || {
    echo '[M197 PUBLIC WIRE FAIL] no real VirtIO DHCP lease' >&2
    exit 1
}
# M196's strict independent parser demands actual guest DNS query+answer,
# a public TCP three-way handshake and TLS payload, not just guest markers.
python3 scripts/m196-public-wire-check.py "$pcap" "$wire_json"
grep -Fq 'M197_APP_WORKLOAD_PASS' "$serial" || {
    echo '[M197 PUBLIC WORKLOAD FAIL] no genuine integrated app result' >&2
    exit 1
}
echo "[M197 ALPINE PUBLIC SOFTWARE LIVE PASS] $port official signed "\
     'Bash Lua SQLite Git from public HTTPS, live DHCP resolver, ' \
     'independent PCAP and four real applications on ext4'
if [ "$m198" = 1 ]; then
    if [ "$m199" = 1 ]; then
        grep -Fq 'M199_FIRST_DURABLE_RC=0' "$serial"
        grep -Fq 'M199_OPENRC_RESTART_RC=0' "$serial"
    else
        grep -Fq 'M198_GUI_DURABLE_RC=0' "$serial"
        grep -Fxq 'M187_DESKTOP_PASS' "$serial"
    fi
    echo "[M198 ALPINE GUI LIVE PASS] $port original Alpine PID1 + public APK "\
        'and old GUI as ordinary process, native VirtIO keyboard/Vim + GPU pixels'
fi

if [ "$m199" = 1 ]; then
    # Retain the first full UART proof before a SECOND original Alpine PID1
    # overwrites the runner's m197-batch.serial.log in the same native matrix.
    cp "$serial" "$PWD/$work/m199-first.serial.log"
    unset NAS_QEMU_SERIAL_FIFO NAS_VIRTIO_NET_PCAP NAS_QEMU_QMP_SOCKET
    export NAS_QEMU_QMP_SOCKET="$PWD/$work/m199-reboot-desktop.qmp"
    export NAS_QEMU_SERIAL_FIFO="$PWD/$work/m199-reboot-serial-input.fifo"
    export BUSYBOX_PARITY_MANIFEST=scripts/m199-alpine-boot-cases.tsv
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M199_REBOOT_SECONDS:-500}
    export NAS_M199_QMP_FAIL_FILE="$PWD/$work/m199-qmp-reboot.failed"
    export NAS_M199_QMP_PASS_FILE="$PWD/$work/m199-qmp-reboot.pass"
    test ! -e "$NAS_QEMU_QMP_SOCKET" &&
    test ! -e "$NAS_M199_QMP_FAIL_FILE" && test ! -e "$NAS_M199_QMP_PASS_FILE"
    python3 scripts/m199-qmp-alpine-openrc.py --mode reboot \
        --serial "$serial" --qmp "$NAS_QEMU_QMP_SOCKET" \
        --outdir "$work" --success-file "$NAS_M199_QMP_PASS_FILE" \
        --timeout "${NAS_M199_DESKTOP_SECONDS:-360}" \
        > "$work/m199-reboot-qmp.log" 2>&1 &
    watcher=$!
    if ! python3 scripts/m199-alpine-uart.py reboot "$runner" "$serial" \
            "$PWD/$work/m199-reboot-runner.log" "$NAS_QEMU_SERIAL_FIFO"; then
        kill "$watcher" 2>/dev/null || :
        wait "$watcher" 2>/dev/null || :
        tail -100 "$work/m199-reboot-qmp.log" >&2 || :
        exit 1
    fi
    if ! wait "$watcher"; then
        echo '[M199 REBOOT QMP FAIL] OpenRC auto-started GPU desktop' >&2
        tail -100 "$work/m199-reboot-qmp.log" >&2
        exit 1
    fi
    grep -Fq 'M199_REBOOT_AUTO_STATUS_RC=0' "$serial"
    grep -Fq 'M199_REBOOT_DURABLE_RC=0' "$serial"
    grep -Fq 'M197_REOPEN_WORKLOAD_PASS' "$serial"
    test -s "$NAS_M199_QMP_PASS_FILE"
    sudo python3 scripts/m199-alpine-disk-check.py "$port" "$root" "$disk"
    sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
    echo "[M199 ALPINE OPENRC TWO BOOT PASS] $port original signed Alpine "\
         'init/OpenRC twice, guest rc-update persisted, OpenRC daemon lifecycle, ' \
         'auto-restored physical GPU, QMP Vim and signed applications on ext4'
    exit 0
fi

# A SECOND native QEMU has NO network requirement; it must observe and run
# the genuine installed ELFs and all SQL/Git/application state from disk.
unset NAS_QEMU_SERIAL_FIFO NAS_VIRTIO_NET_PCAP NAS_QEMU_QMP_SOCKET
if [ "$m198" = 1 ]; then
    export BUSYBOX_PARITY_MANIFEST=scripts/m198-alpine-reopen-cases.tsv
else
    export BUSYBOX_PARITY_MANIFEST=scripts/m197-alpine-reopen-cases.tsv
fi
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M197_REOPEN_SECONDS:-240}
if ! sh "$runner" > "$PWD/$work/m197-reopen-runner.log" 2>&1; then
    echo "[M197 ALPINE REOPEN FAIL] $port second native QEMU" >&2
    tail -120 "$PWD/$work/m197-reopen-runner.log" >&2
    exit 1
fi
reopen="$PWD/$logs/m197-batch.clean.log"
for marker in \
    'M197_REOPEN_WORKLOAD_PASS' \
    'M197_PUBLIC_ALPINE_SOFTWARE_2026' \
    '[M191 ROOT] physical Alpine ext4 registered at /' \
    '[LWEXT4 GUEST] clean upstream unmount and durable flush' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0'; do
    grep -Fxq "$marker" "$reopen" || {
        echo "[M197 ALPINE REOPEN FAIL] $port missing $marker" >&2
        tail -120 "$reopen" >&2
        exit 1
    }
done
if [ "$m198" = 1 ]; then
    grep -Fxq 'M198_GUI_REOPEN_PASS' "$reopen" || {
        echo '[M198 REOPEN FAIL] native editor did not persist' >&2; exit 1;
    }
fi
sudo python3 scripts/m197-alpine-disk-check.py "$port" "$root" "$disk"
if [ "$m198" = 1 ]; then
    sudo python3 scripts/m198-alpine-disk-check.py "$port" "$root" "$disk"
fi
sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
echo "[M197 ALPINE PUBLIC SOFTWARE PERSISTENCE PASS] $port original "\
     'Alpine public signed four-package transaction, native Bash/Lua/SQLite/Git, ' \
     'second real QEMU, exact Git+SQL host disk oracle and ext4 fsck'
if [ "$m198" = 1 ]; then
    echo "[M198 ALPINE GUI REBOOT PASS] $port original PID1, publicly signed "\
        'Bash/Lua/SQLite/Git and desktop dependencies, physical QMP/Vim, ' \
        'second real QEMU, exact host ext4 byte and fsck oracle'
fi
