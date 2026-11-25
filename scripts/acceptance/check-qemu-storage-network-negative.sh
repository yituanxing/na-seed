#!/bin/sh
# H7–H11: preserve each physical storage/network negative proof, with no
# success from a fallback device or rescue userspace.
set -eu
[ "$#" -eq 2 ] || exit 2
profile=$1
case "$profile:$2" in
  h7:no-nvme)
    logmode='no-nvme'
    zero_flags='NAS_QEMU_H7_NVME NAS_QEMU_H2_NVME NAS_QEMU_H4_NVME NAS_QEMU_H5_PCI_NVME NAS_QEMU_H6_TWO_NVME'
    empty_flags='NAS_VIRTIO_BLK_IMAGE NAS_QEMU_ASYNC_BLK_IMAGE'
    negative_flag=''
    seconds='32'
    tail_lines='100'
    expected='[H3 NVME FAIL] required QEMU admin test failed'
    forbidden='H7 NVME EXT4 MOUNTED|H7_ORIGINAL_TWO_ELFS_EXECUTED_FROM_NVME|\[EXEC\] replaced process with lwext4 ELF: /mnt/bin/'
    ambient_message='[H7 NEGATIVE FAIL] unexpected ambient disk'
    stop_message='[H7 NEGATIVE FAIL] QEMU did not cleanly fail/halt rc='
    missing_message='[H7 NEGATIVE FAIL] missing no-device rejection or userland ran without NVMe'
    fallback_message='[H7 NEGATIVE FAIL] missing no-device rejection or userland ran without NVMe'
    pass_message='[H7 NO-NVME FAIL-CLOSED PASS] absent NVMe refused before mounted userland; CPIO rescue is not a substitute'
    ;;
  h8:no-nvme)
    logmode='no-nvme'
    zero_flags='NAS_QEMU_H8_NVME NAS_QEMU_H7_NVME NAS_QEMU_H2_NVME NAS_QEMU_H4_NVME NAS_QEMU_H5_PCI_NVME NAS_QEMU_H6_TWO_NVME'
    empty_flags='NAS_VIRTIO_BLK_IMAGE NAS_QEMU_ASYNC_BLK_IMAGE'
    negative_flag=''
    seconds='45'
    tail_lines='120'
    expected='[H3 NVME FAIL] required QEMU admin test failed'
    forbidden='H8 NVME ALPINE ROOT MOUNTED|H8_ORIGINAL_ALPINE_DYNAMIC_ELFS_NVME_PASS|H8_ALPINE_(LUA54|JQ|SQLITE)_NVME_PASS|\[EXEC\] replaced process with lwext4 ELF: /bin/'
    ambient_message='[H8 NEGATIVE FAIL] ambient block controller/profile supplied'
    stop_message='[H8 NEGATIVE FAIL] QEMU did not fail/halt within bound rc='
    missing_message='[H8 NEGATIVE FAIL] missing real no-device NVMe rejection'
    fallback_message='[H8 NEGATIVE FAIL] userland executed without genuine NVMe'
    pass_message='[H8 NO-NVME FAIL-CLOSED PASS] no NVMe -> no Alpine ELF, no ext4 / mount, no CPIO substitution'
    ;;
  h9:no-ahci)
    logmode='no-sata'
    zero_flags='NAS_QEMU_H9_AHCI'
    empty_flags='NAS_VIRTIO_BLK_IMAGE NAS_QEMU_ASYNC_BLK_IMAGE'
    negative_flag=''
    seconds='45'
    tail_lines='130'
    expected='[H9 AHCI FAIL] required QEMU SATA disk unavailable'
    forbidden='H9 AHCI ROOT MOUNTED|H9_ORIGINAL_ALPINE_DYNAMIC_ELFS_SATA_PASS|H9_ALPINE_(LUA54|JQ|SQLITE)_SATA_PASS|\[EXEC\] replaced process with lwext4 ELF: /bin/'
    ambient_message='[H9 NEGATIVE FAIL] ambient SATA/VirtIO disk was supplied'
    stop_message='[H9 NEGATIVE FAIL] QEMU did not refuse/halt within bound rc='
    missing_message='[H9 NEGATIVE FAIL] missing explicit real no-HDD rejection'
    fallback_message='[H9 NEGATIVE FAIL] Alpine userland ran without a genuine SATA disk'
    pass_message='[H9 NO-SATA FAIL-CLOSED PASS] built-in Q35 AHCI and ATAPI ISO alone cannot supply Alpine SATA ext4 /; no CPIO/NVMe/VirtIO substitute'
    ;;
  h10:no-e1000)
    logmode='no-e1000'
    zero_flags='NAS_QEMU_H10_E1000 NAS_VIRTIO_NET_USER'
    empty_flags=''
    negative_flag='NAS_QEMU_H10_NO_E1000'
    seconds='45'
    tail_lines='150'
    expected='[H10 E1000 FAIL] required QEMU E1000 missing; no Alpine network userland'
    forbidden='H10_E1000_ORIGINAL_ALPINE_WGET_PASS|H10_E1000_HTTP_SHA256|H10 E1000 NETDEV ATTACHED|\[LWIP DHCP BOUND\]'
    ambient_message='[H10 NEGATIVE FAIL] ambient NIC supplied'
    stop_message='[H10 NEGATIVE FAIL] unexpected no-NIC QEMU rc='
    missing_message='[H10 NEGATIVE FAIL] missing explicit PCI E1000 absence rejection'
    fallback_message='[H10 NEGATIVE FAIL] network userland ran without the E1000'
    pass_message='[H10 NO-E1000 FAIL-CLOSED PASS] same genuine signed Alpine NVMe root; no ambient Q35 E1000e; PCI E1000 absence blocks original wget'
    ;;
  h11:no-usb-media)
    logmode='no-media'
    zero_flags='NAS_QEMU_H11_USB_POSITIVE NAS_QEMU_H11_NO_XHCI'
    empty_flags='NAS_VIRTIO_BLK_IMAGE NAS_QEMU_ASYNC_BLK_IMAGE'
    negative_flag='NAS_QEMU_H11_NO_MEDIA'
    seconds='60'
    tail_lines='180'
    expected='[H11 USB FAIL] required QEMU USB Mass Storage unavailable'
    forbidden='H11 USB ROOT MOUNTED|H11_ORIGINAL_ALPINE_DYNAMIC_ELFS_USB_PASS|H11_ALPINE_(BUSYBOX|LUA54|JQ|SQLITE)_USB_PASS|\[EXEC\] replaced process with lwext4 ELF: /bin/'
    ambient_message='[H11 NO-MEDIA FAIL] ambient block backend supplied'
    stop_message='[H11 NO-MEDIA FAIL] real QEMU did not refuse/halt within bound rc='
    missing_message='[H11 NO-MEDIA FAIL] controller-only boot lacks explicit USB media refusal'
    fallback_message='[H11 NO-MEDIA FAIL] original Alpine ran without real USB media'
    pass_message='[H11 NO-USB-MEDIA FAIL-CLOSED PASS] genuine QEMU xHCI with no storage cannot mount Alpine ext4 / or execute original ELF'
    ;;
  h11:no-xhci)
    logmode='no-xhci'
    zero_flags='NAS_QEMU_H11_USB_POSITIVE NAS_QEMU_H11_NO_MEDIA'
    empty_flags='NAS_VIRTIO_BLK_IMAGE NAS_QEMU_ASYNC_BLK_IMAGE'
    negative_flag='NAS_QEMU_H11_NO_XHCI'
    seconds='60'
    tail_lines='180'
    expected='[H11 USB FAIL] required QEMU USB Mass Storage unavailable'
    forbidden='H11 USB ROOT MOUNTED|H11_ORIGINAL_ALPINE_DYNAMIC_ELFS_USB_PASS|H11_ALPINE_(BUSYBOX|LUA54|JQ|SQLITE)_USB_PASS|\[EXEC\] replaced process with lwext4 ELF: /bin/'
    ambient_message='[H11 NO-XHCI FAIL] ambient block backend supplied'
    stop_message='[H11 NO-XHCI FAIL] real QEMU did not refuse/halt within bound rc='
    missing_message='[H11 NO-XHCI FAIL] missing explicit no-controller refusal'
    fallback_message='[H11 NO-XHCI FAIL] Alpine ran without a genuine USB host controller'
    pass_message='[H11 NO-XHCI FAIL-CLOSED PASS] real QEMU with no xHCI or USB media cannot mount Alpine ext4 / or execute original ELF'
    ;;
  *) exit 2 ;;
esac
out=build/$profile-qemu-only
image="$out/naseed.iso"
serial_log="$out/$profile-$logmode-uefi-serial.log"
clean="$out/$profile-$logmode-uefi-clean.log"
test -s "$image" && [ ! -L "$serial_log" ] || exit 2
[ "$profile" = h7 ] || [ ! -L "$image" ] || exit 2
for flag in $empty_flags; do
    eval 'value=${'"$flag"':-}'
    [ -z "$value" ] || { echo "$ambient_message" >&2; exit 2; }
done
for flag in $zero_flags; do
    eval 'value=${'"$flag"':-0}'
    [ "$value" = 0 ] || { echo "$ambient_message" >&2; exit 2; }
done
if [ "$profile" = h9 ]; then
    for flag in NAS_QEMU_H2_NVME NAS_QEMU_H4_NVME NAS_QEMU_H5_PCI_NVME \
                NAS_QEMU_H6_TWO_NVME NAS_QEMU_H7_NVME NAS_QEMU_H8_NVME; do
        eval 'value=${'"$flag"'-0}'
        [ "$value" = 0 ] || { echo "[H9 NEGATIVE FAIL] ambient NVMe profile $flag" >&2; exit 2; }
    done
fi
rc=0
if [ -n "$negative_flag" ]; then
    env "$negative_flag=1" timeout "${seconds}s" sh scripts/run.sh "$image" > "$serial_log" 2>&1 || rc=$?
else
    timeout "${seconds}s" sh scripts/run.sh "$image" > "$serial_log" 2>&1 || rc=$?
fi
tr -d '\r' < "$serial_log" > "$clean"
fail() { echo "$1" >&2; tail -n "$tail_lines" "$clean" >&2; exit 1; }
case "$rc" in 1|124) ;; *) fail "$stop_message$rc" ;; esac
grep -Fq "$expected" "$clean" || fail "$missing_message"
if grep -Eq "$forbidden" "$clean"; then fail "$fallback_message"; fi
echo "$pass_message"
