#!/bin/sh
# Dry-run the REAL x86 build graph. The upstream lwIP/Netconn provider is
# always linked; only the U-Boot NIC and BlockDevice attachment are optional.
set -eu
if make -n NAS_ENABLE_LWIP=0 kernel >/dev/null 2>&1; then
    echo '[NETWORK BUILD FAIL] retired legacy AF_INET opt-out accepted' >&2
    exit 1
fi
for configuration in default net blk both; do
    case "$configuration" in
        default) flags='' ;;
        net) flags='NAS_ENABLE_VIRTIO_NET=1' ;;
        blk) flags='NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_LWEXT4=1' ;;
        both) flags='NAS_ENABLE_VIRTIO_NET=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_LWEXT4=1' ;;
    esac
    # Intentionally split known make VAR=value tokens from flags.
    make -n $flags kernel | awk -v mode="$configuration" '
      /^clang / && / -c / {
        if(index($0," -c ports/uboot/net_port.c "))nic++;
        if(index($0," -c ports/uboot/blk_port.c "))blk++;
        if(index($0," -c ports/uboot/virtio_compat.c "))common++;
        if(index($0," -c kernel/drivers/port/net.c "))netdev++;
        if(index($0," -c ports/lwip/netconn_bridge.c "))netconn++;
        if(index($0," -c ports/lwip/socket_port.c "))socketport++;
        if(index($0," -c kernel/net/ipv4_loopback.c "))legacy++;
      }
      END {
        needed_nic=(mode=="net" || mode=="both");
        needed_blk=(mode=="blk" || mode=="both");
        needed_netconn=1;
        if(nic!=needed_nic || blk!=needed_blk ||
           common!=(needed_nic || needed_blk) || legacy!=0 ||
           netdev!=1 || netconn!=needed_netconn || socketport!=needed_netconn)
          exit 1;
        print "[NETWORK BUILD SELECT PASS] " mode ": shared transport/netdev once, correct class/Netconn/FileOps selection";
      }'
done
