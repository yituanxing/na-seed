#!/bin/sh
# Host-only production-source fixtures; native QEMU proof remains separate.
set -eu
[ "$#" -eq 1 ] || exit 2
profile=$1
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
    *) exit 2 ;;
esac
mkdir -p "${output%/*}"
cc -std=c11 -O2 -Wall -Wextra -Werror "$@" -o "$output"
"$output"
