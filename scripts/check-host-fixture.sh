#!/bin/sh
# Host-only production-source fixtures; native QEMU proof remains separate.
set -eu
[ "$#" -eq 1 ] || exit 2
profile=$1
# Keep the Linux oracle entry points runnable from outside the repository.
case "$profile" in a008-linux-vectors|a009-a010-linux-fcntl|a012-linux-socket-stream|a014-linux-blocked-readv|packet-socket) cd "$(dirname "$0")/.." ;; esac
compiler=cc
standard=c11
optimization=-O2
case "$profile" in
    bootfs-scale)
        output=build/tests/bootfs-scale
        set -- -I kernel scripts/test-bootfs-scale.c kernel/bootfs.c ;;
    m141-pidfd-host)
        output=build/m141-pidfd/test
        set -- -fno-builtin -I kernel scripts/test-m141-pidfd.c kernel/fs/pidfd.c kernel/fd.c ;;
    m202-input-routing)
        output=build/m202-input-host/m202-input-routing
        set -- -fno-builtin -I kernel scripts/test-m202-input-routing.c kernel/drivers/input/core.c kernel/drivers/input/evdev.c kernel/mm/heap.c kernel/lib/string.c ;;
    m86-txn)
        output=build/m86-txn/check
        set -- -fno-builtin -ffunction-sections -fdata-sections -Wl,--gc-sections -I kernel scripts/test-m86-txn.c kernel/mm/vm.c kernel/mm/anon_regions.c kernel/mm/heap.c ;;
    netlink-filter)
        output=build/netlink-filter/test-netlink-filter
        set -- -DNAS_ENABLE_LWIP=1 -I kernel scripts/test-netlink-filter.c kernel/fs/netlink_filter.c kernel/lib/string.c ;;
    netlink-generic)
        output=build/netlink-generic/test-netlink-generic
        set -- -DNAS_ENABLE_LWIP=1 -I kernel scripts/test-netlink-generic.c kernel/fs/netlink_generic.c kernel/lib/string.c ;;
    path-request-host)
        output=build/path-request-host/test
        set -- -fno-builtin -ffunction-sections -fdata-sections -I kernel -I scripts/m89-test-include scripts/test-path-request-host.c kernel/fs/namespace_syscall.c kernel/mm/heap.c -Wl,--gc-sections ;;
    x86-nx)
        output=build/x86-nx/check
        set -- -fno-builtin -ffunction-sections -fdata-sections -Wl,--gc-sections -I kernel scripts/test-x86-nx.c kernel/arch/x86_64/mmu.c ;;
    m191-root-provider)
        . scripts/vfs-host-sources.sh
        output=build/m191/test-root-provider
        set -- -fno-builtin -I kernel scripts/test-m191-root-provider.c $NAS_VFS_HOST_SOURCES ;;
    sysfs-block)
        . scripts/vfs-host-sources.sh
        output=build/sysfs-block/test
        set -- -fno-builtin -I kernel scripts/test-sysfs-block.c $NAS_VFS_HOST_SOURCES ;;
    h3-nvme-identify-host|h4-nvme-readonly-host|h9-ahci-readonly-host|h10-e1000-host|h13-mouse-host|h13-xhci-host|h14-xhci-host)
        # All seven hardware fixtures previously compiled a temporary binary.
        tmp=$(mktemp -d "${TMPDIR:-/tmp}/naseed-host-fixture.XXXXXX")
        trap 'rm -rf "$tmp"' EXIT HUP INT TERM
        output=$tmp/test
        case "$profile" in
            h3-nvme-identify-host) set -- -I kernel -I kernel/drivers -I kernel/drivers/nvme tests/h3-nvme-identify-host.c kernel/drivers/nvme/identify.c kernel/drivers/port/dma.c ;;
            h4-nvme-readonly-host) set -- -I kernel -I kernel/drivers -I kernel/drivers/nvme tests/h4-nvme-readonly-host.c kernel/drivers/nvme/readonly.c kernel/drivers/port/dma.c ;;
            h9-ahci-readonly-host) set -- -I kernel -I kernel/drivers -I kernel/drivers/ahci tests/h9-ahci-readonly-host.c kernel/drivers/ahci/readonly.c kernel/drivers/port/dma.c ;;
            h10-e1000-host) set -- -I kernel -I kernel/drivers -I kernel/drivers/e1000 tests/h10-e1000-host.c kernel/drivers/e1000/e1000_nic.c kernel/drivers/port/dma.c ;;
            h13-mouse-host) set -- -fno-builtin -I kernel tests/h13-hid-mouse-host.c kernel/drivers/usb/hid_boot_mouse.c kernel/drivers/input/core.c kernel/drivers/input/evdev.c kernel/mm/heap.c kernel/lib/string.c ;;
            h13-xhci-host) set -- -fno-builtin -I kernel tests/h13-xhci-mouse-host.c kernel/drivers/usb/xhci.c kernel/drivers/port/dma.c ;;
            h14-xhci-host) set -- -fno-builtin -I kernel tests/h14-xhci-dual-host.c kernel/drivers/usb/xhci.c kernel/drivers/port/dma.c ;;
        esac
        "${CC:-cc}" -std=c11 -O2 -g -Wall -Wextra -Werror "$@" -o "$output"
        "$output"
        exit 0 ;;
    m111-getrusage)
        output=build/m111-rusage/host
        set -- -ffunction-sections -fdata-sections -fno-builtin -I kernel scripts/test-m111-getrusage.c kernel/task/process.c -Wl,--gc-sections ;;
    m187-inotify)
        output=build/m187-inotify/test
        standard=gnu11
        set -- -fno-builtin -I kernel scripts/test-m187-inotify-kernel.c kernel/lib/string.c ;;
    pipe-capacity)
        output=build/pipe-capacity/test
        set -- -fno-builtin -I kernel scripts/test-pipe-capacity.c kernel/pipe.c kernel/mm/heap.c kernel/lib/string.c ;;
    a008-linux-vectors)
        output=/tmp/naseed-a008-linux-vectors
        set -- scripts/test-a008-linux-vectors.c ;;
    a009-a010-linux-fcntl)
        output=/tmp/naseed-a009-a010-linux-fcntl
        set -- -static -no-pie scripts/test-a009-a010-linux-fcntl.c ;;
    a012-linux-socket-stream)
        output=/tmp/naseed-a012-linux-socket-stream
        set -- scripts/test-a012-linux-socket-stream.c ;;
    a014-linux-blocked-readv)
        output=/tmp/naseed-a014-linux-blocked-readv
        set -- -pthread scripts/test-a014-linux-blocked-readv.c ;;
    timerfd|pt-directory|m203-fbcon|h0-acpi-host|packet-socket)
        tmp=$(mktemp -d "${TMPDIR:-/tmp}/naseed-host-fixture.XXXXXX")
        trap 'rm -rf "$tmp"' EXIT HUP INT TERM
        case "$profile" in
            timerfd) optimization=; output="$tmp/timerfd"; set -- -ffreestanding -I kernel scripts/test-timerfd-host.c kernel/fs/timerfd.c ;;
            pt-directory)  output="$tmp/pt"; set -- scripts/test-pt-directory.c kernel/arch/aarch64/mmu.c kernel/arch/riscv64/mmu.c kernel/arch/aarch64/mmu_test.c kernel/arch/riscv64/mmu_test.c ;;
            m203-fbcon) compiler=gcc; output="$tmp/fbcon-test"; set -- -g -ffreestanding -fno-builtin -I . -I kernel -fsanitize=address,undefined -fno-omit-frame-pointer scripts/test-m203-fbcon.c kernel/drivers/display/fbcon.c ;;
            h0-acpi-host)  output="$tmp/h0-acpi-host"; set -- -Wpedantic tests/h0-acpi-host.c kernel/platform/firmware_acpi.c ;;
            packet-socket)  output="$tmp/packet-socket"; set -- -I kernel scripts/test-packet-socket.c kernel/fs/packet_socket.c kernel/drivers/port/net.c ;;
        esac ;;
    *) exit 2 ;;
esac
mkdir -p "${output%/*}"
"$compiler" -std="$standard" $optimization -Wall -Wextra -Werror "$@" -o "$output"
case "$profile" in
    m203-fbcon) ASAN_OPTIONS=detect_leaks=0 "$output" ;;
    a014-linux-blocked-readv)
        # Repeat the real thread race, each process has its own watchdog.
        for attempt in 1 2 3 4 5; do "$output"; done
        printf '[A014 LINUX OWNERSHIP ORACLE PASS] 5x iov and fd rebinding while readv blocked\n' ;;
    *) "$output" ;;
esac
case "$profile" in
    m111-getrusage)
        cc -std=c11 -Wall -Wextra -Werror -fsyntax-only -I kernel kernel/task.c
        cc -std=c11 -Wall -Wextra -Werror -fsyntax-only -I kernel kernel/syscall/dispatch.c ;;
    m187-inotify)
        cc -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin \
          -I scripts/m89-test-include -fsyntax-only \
          kernel/fs/inotify.c kernel/fs/vfs.c kernel/fs/syscall.c kernel/fs/file_io.c ;;
    pipe-capacity)
        cc -std=c11 -O2 -Wall -Wextra -Werror scripts/test-pipe-fcntl-linux.c -o build/pipe-capacity/linux-oracle
        build/pipe-capacity/linux-oracle ;;
    timerfd) echo '[TIMERFD HOST PASS] clock, expiry, poll, read, dup/close/reuse, three-ISA shared provider' ;;
    a009-a010-linux-fcntl) echo '[A009/A010 LINUX ORACLE PASS] native fcntl + positional append' ;;
esac
