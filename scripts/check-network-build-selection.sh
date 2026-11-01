#!/bin/sh
# Dry-run the REAL x86 build graph. Validate independent opt-ins and no
# duplicate shared NIC/device-model TUs before expensive three-ISA QEMU.
set -eu
for configuration in net blk both; do
    case "$configuration" in
        net) flags='NAS_ENABLE_VIRTIO_NET=1 NAS_ENABLE_LWIP=1' ;;
        blk) flags='NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_LWEXT4=1' ;;
        both) flags='NAS_ENABLE_VIRTIO_NET=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_LWEXT4=1 NAS_ENABLE_LWIP=1' ;;
    esac
    # Intentionally split known make VAR=value tokens from flags.
    make -n $flags kernel | awk -v mode="$configuration" '
      /^clang / && / -c / {
        if(index($0," -c ports/uboot/net_port.c "))nic++;
        if(index($0," -c ports/uboot/blk_port.c "))blk++;
        if(index($0," -c ports/uboot/virtio_compat.c "))common++;
        if(index($0," -c kernel/drivers/port/net.c "))netdev++;
      }
      END {
        needed_nic=(mode!="blk");
        needed_blk=(mode!="net");
        if(nic!=needed_nic || blk!=needed_blk || common!=1 || netdev!=1)
          exit 1;
        print "[NETWORK BUILD SELECT PASS] " mode ": one shared transport and netdev, class-specific source only";
      }'
done
