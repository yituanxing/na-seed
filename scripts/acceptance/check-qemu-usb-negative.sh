#!/bin/sh
# Exact hardware-negative profiles: genuine QEMU, no userspace or fallback.
set -eu
[ "$#" -eq 2 ] || exit 2
profile=$1
missing=$2
case "$profile:$missing" in
  h12:no-usb-kbd)
    zero_flags='NAS_QEMU_H12_USB_KBD_POSITIVE NAS_QEMU_H12_NO_XHCI NAS_QEMU_INPUT_OPTIN'
    empty_flags=NAS_VIRTIO_BLK_IMAGE
    expected='[H12 HID FAIL] required QEMU USB Boot Keyboard unavailable'
    negative_flag=NAS_QEMU_H12_NO_USB_KBD
    forbidden='H12 HID INPUT ATTACHED|H12_HID_READY_FOR_QMP|H12_HID_A_(DOWN|UP)_WITH_SYN|H12_ORIGINAL_ALPINE_EVTEST_USB_PASS|\[EXEC\] replaced process with lwext4 ELF: /usr/bin/evtest|\[M1\] entering actual ring 3|\[EXIT\] user status=0|\[PASS\] cmdline userspace exit 0'
    ambient_message='[H12 NO-USB-KBD FAIL] ambient input or block device supplied'
    missing_message='[H12 NO-USB-KBD FAIL] missing explicit xHCI no-keyboard rejection'
    hardware_message='[H12 NO-USB-KBD FAIL] actual QEMU xHCI controller/bar not present'
    fallback_message='[H12 NO-USB-KBD FAIL] original evtest saw input without real USB keyboard'
    pass_message='[H12 NO-USB-KBD FAIL-CLOSED PASS] genuine xHCI controller without keyboard cannot produce evdev event0/original evtest USB input; NVMe root alone insufficient'
    bar='[H12 HID BAR QUALIFIED]'
    ;;
  h12:no-xhci)
    zero_flags='NAS_QEMU_H12_USB_KBD_POSITIVE NAS_QEMU_H12_NO_USB_KBD NAS_QEMU_INPUT_OPTIN'
    empty_flags=NAS_VIRTIO_BLK_IMAGE
    expected='[H12 HID FAIL] required unique QEMU xHCI absent or ambiguous'
    negative_flag=NAS_QEMU_H12_NO_XHCI
    forbidden='\[H0 PCI\] 00:04\.0 vendor=1b36 device=000d|\[H12 HID BAR QUALIFIED\]|H12 HID INPUT ATTACHED|H12_HID_READY_FOR_QMP|H12_HID_A_(DOWN|UP)_WITH_SYN|H12_ORIGINAL_ALPINE_EVTEST_USB_PASS|\[EXEC\] replaced process with lwext4 ELF: /usr/bin/evtest|\[M1\] entering actual ring 3|\[EXIT\] user status=0|\[PASS\] cmdline userspace exit 0'
    ambient_message='[H12 NO-XHCI FAIL] ambient input or block device supplied'
    missing_message='[H12 NO-XHCI FAIL] missing explicit absent-controller refusal'
    hardware_message=''
    fallback_message='[H12 NO-XHCI FAIL] original evtest received events with no USB host controller'
    pass_message='[H12 NO-XHCI FAIL-CLOSED PASS] genuine QEMU with no USB host controller cannot create USB event0 or execute signed evtest input workflow'
    bar=''
    ;;
  h13:no-usb-mouse)
    zero_flags='NAS_QEMU_H13_USB_MOUSE_USERLAND NAS_QEMU_H13_XHCI_MISSING NAS_QEMU_INPUT_OPTIN'
    empty_flags=NAS_VIRTIO_BLK_IMAGE
    expected='[H13 MOUSE FAIL] required QEMU USB Boot Mouse unavailable'
    negative_flag=NAS_QEMU_H13_USB_MOUSE_MISSING
    forbidden='H13 MOUSE INPUT ATTACHED|H13_MOUSE_(READY_FOR_QMP|REL_X_WITH_SYN|REL_Y_WITH_SYN|BTN_DOWN_WITH_SYN|BTN_UP_WITH_SYN)|H13_ORIGINAL_ALPINE_MOUSE_EVTEST_USB_PASS|\[EXEC\] replaced process with lwext4 ELF: /usr/bin/evtest|\[M1\] entering actual ring 3|\[EXIT\] user status=0|\[PASS\] cmdline userspace exit 0'
    ambient_message='[H13 NO-USB-MOUSE FAIL] ambient input or block device supplied'
    missing_message='[H13 NO-USB-MOUSE FAIL] missing explicit xHCI no-mouse rejection'
    hardware_message='[H13 NO-USB-MOUSE FAIL] actual QEMU xHCI controller/bar not present'
    fallback_message='[H13 NO-USB-MOUSE FAIL] original evtest saw input without real USB mouse'
    pass_message='[H13 NO-USB-MOUSE FAIL-CLOSED PASS] genuine xHCI controller without USB Boot Mouse cannot produce evdev event0/original evtest USB input; NVMe root alone insufficient'
    bar='[H13 MOUSE BAR QUALIFIED]'
    ;;
  h13:no-xhci)
    zero_flags='NAS_QEMU_H13_USB_MOUSE_USERLAND NAS_QEMU_H13_USB_MOUSE_MISSING NAS_QEMU_INPUT_OPTIN'
    empty_flags=NAS_VIRTIO_BLK_IMAGE
    expected='[H13 MOUSE FAIL] required unique QEMU xHCI absent or ambiguous'
    negative_flag=NAS_QEMU_H13_XHCI_MISSING
    forbidden='\[H0 PCI\] 00:04\.0 vendor=1b36 device=000d|\[H13 MOUSE BAR QUALIFIED\]|H13 MOUSE INPUT ATTACHED|H13_MOUSE_(READY_FOR_QMP|REL_X_WITH_SYN|REL_Y_WITH_SYN|BTN_DOWN_WITH_SYN|BTN_UP_WITH_SYN)|H13_ORIGINAL_ALPINE_MOUSE_EVTEST_USB_PASS|\[EXEC\] replaced process with lwext4 ELF: /usr/bin/evtest|\[M1\] entering actual ring 3|\[EXIT\] user status=0|\[PASS\] cmdline userspace exit 0'
    ambient_message='[H13 NO-XHCI FAIL] ambient input or block device supplied'
    missing_message='[H13 NO-XHCI FAIL] missing explicit absent-controller refusal'
    hardware_message=''
    fallback_message='[H13 NO-XHCI FAIL] original evtest received events with no USB host controller'
    pass_message='[H13 NO-XHCI FAIL-CLOSED PASS] genuine QEMU with no USB host controller cannot create USB event0 or execute signed evtest input workflow'
    bar=''
    ;;
  h14:no-usb-kbd)
    zero_flags='NAS_QEMU_H14_DUAL_HID_USERLAND NAS_QEMU_H14_NO_XHCI NAS_QEMU_H14_NO_USB_MOUSE NAS_QEMU_INPUT_OPTIN'
    empty_flags=NAS_VIRTIO_BLK_IMAGE
    expected='[H14 DUAL FAIL] required USB Boot Keyboard unavailable'
    negative_flag=NAS_QEMU_H14_NO_USB_KBD
    forbidden='H14_(KBD|MOUSE)_(READY_FOR_QMP|A_DOWN_WITH_SYN|A_UP_WITH_SYN|REL_X_WITH_SYN|REL_Y_WITH_SYN|BTN_DOWN_WITH_SYN|BTN_UP_WITH_SYN)|H14_ORIGINAL_ALPINE_DUAL_EVTEST_USB_PASS|\[EXEC\] replaced process with lwext4 ELF: /usr/bin/evtest|\[M1\] entering actual ring 3|\[EXIT\] user status=0|\[PASS\] cmdline userspace exit 0'
    ambient_message='[H14 NO-USB-KBD FAIL] ambient input or block device supplied'
    missing_message='[H14 NO-USB-KBD FAIL] missing explicit xHCI missing keyboard rejection'
    hardware_message='[H14 NO-USB-KBD FAIL] actual QEMU xHCI controller/bar not present'
    fallback_message='[H14 NO-USB-KBD FAIL] original evtest entered userland without both real USB HID devices'
    pass_message='[H14 NO-USB-KBD FAIL-CLOSED PASS] genuine xHCI controller without both required USB HID devices cannot enter original evtest userland; NVMe root alone insufficient'
    bar='[H14 DUAL BAR QUALIFIED]'
    ;;
  h14:no-usb-mouse)
    zero_flags='NAS_QEMU_H14_DUAL_HID_USERLAND NAS_QEMU_H14_NO_XHCI NAS_QEMU_H14_NO_USB_KBD NAS_QEMU_INPUT_OPTIN'
    empty_flags=NAS_VIRTIO_BLK_IMAGE
    expected='[H14 DUAL FAIL] required USB Boot Mouse unavailable'
    negative_flag=NAS_QEMU_H14_NO_USB_MOUSE
    forbidden='H14_(KBD|MOUSE)_(READY_FOR_QMP|A_DOWN_WITH_SYN|A_UP_WITH_SYN|REL_X_WITH_SYN|REL_Y_WITH_SYN|BTN_DOWN_WITH_SYN|BTN_UP_WITH_SYN)|H14_ORIGINAL_ALPINE_DUAL_EVTEST_USB_PASS|\[EXEC\] replaced process with lwext4 ELF: /usr/bin/evtest|\[M1\] entering actual ring 3|\[EXIT\] user status=0|\[PASS\] cmdline userspace exit 0'
    ambient_message='[H14 NO-USB-MOUSE FAIL] ambient input or block device supplied'
    missing_message='[H14 NO-USB-MOUSE FAIL] missing explicit xHCI missing mouse rejection'
    hardware_message='[H14 NO-USB-MOUSE FAIL] actual QEMU xHCI controller/bar not present'
    fallback_message='[H14 NO-USB-MOUSE FAIL] original evtest entered userland without both real USB HID devices'
    pass_message='[H14 NO-USB-MOUSE FAIL-CLOSED PASS] genuine xHCI controller without both required USB HID devices cannot enter original evtest userland; NVMe root alone insufficient'
    bar='[H14 DUAL BAR QUALIFIED]'
    ;;
  h14:no-xhci)
    zero_flags='NAS_QEMU_H14_DUAL_HID_USERLAND NAS_QEMU_H14_NO_USB_MOUSE NAS_QEMU_H14_NO_USB_KBD NAS_QEMU_INPUT_OPTIN'
    empty_flags=NAS_VIRTIO_BLK_IMAGE
    expected='[H14 DUAL FAIL] required unique QEMU xHCI absent or ambiguous'
    negative_flag=NAS_QEMU_H14_NO_XHCI
    forbidden='\[H0 PCI\] 00:04\.0 vendor=1b36 device=000d|\[H14 DUAL BAR QUALIFIED\]|H14 DUAL KEYBOARD ATTACHED|H14 DUAL MOUSE ATTACHED|H14_(KBD|MOUSE)_(READY_FOR_QMP|A_DOWN_WITH_SYN|A_UP_WITH_SYN|REL_X_WITH_SYN|REL_Y_WITH_SYN|BTN_DOWN_WITH_SYN|BTN_UP_WITH_SYN)|H14_ORIGINAL_ALPINE_DUAL_EVTEST_USB_PASS|\[EXEC\] replaced process with lwext4 ELF: /usr/bin/evtest|\[M1\] entering actual ring 3|\[EXIT\] user status=0|\[PASS\] cmdline userspace exit 0'
    ambient_message='[H14 NO-XHCI FAIL] ambient input or block device supplied'
    missing_message='[H14 NO-XHCI FAIL] missing explicit absent-controller refusal'
    hardware_message=''
    fallback_message='[H14 NO-XHCI FAIL] original evtest received events with no USB host controller'
    pass_message='[H14 NO-XHCI FAIL-CLOSED PASS] genuine QEMU with no USB host controller cannot create USB event0 or execute signed evtest input workflow'
    bar=''
    ;;
  h15:no-usb-serial)
    zero_flags='NAS_QEMU_H15_USB_SERIAL_USERLAND NAS_QEMU_H15_NO_USB_SERIAL NAS_QEMU_H15_NO_XHCI NAS_QEMU_INPUT_OPTIN'
    empty_flags='NAS_VIRTIO_BLK_IMAGE NAS_QEMU_H15_FTDI_SOCKET'
    expected='[H15 FTDI FAIL] required QEMU FT232BM 0403:6001 unavailable; no original Alpine userland'
    negative_flag=NAS_QEMU_H15_NO_USB_SERIAL
    forbidden='H15_USB_SERIAL_READY_FOR_HOST|H15_FTDI_ORIGINAL_BUSYBOX_STTY_PASS|H15_ORIGINAL_ALPINE_BUSYBOX_USB_SERIAL_NONCE_RETURNED|\[H15 USB SERIAL ATTACHED\]|\[EXEC\] replaced process with lwext4 ELF: /bin/busybox|\[M1\] entering actual ring 3|\[EXIT\] user status=0|\[PASS\] cmdline userspace exit 0'
    ambient_message='[H15 NO-USB-SERIAL FAIL] ambient serial or block fixture supplied'
    missing_message='[H15 NO-USB-SERIAL FAIL] missing explicit guest USB serial hardware rejection'
    hardware_message='[H15 NO-USB-SERIAL FAIL] genuine xHCI PCI controller BAR was not present'
    fallback_message='[H15 NO-USB-SERIAL FAIL] original signed Alpine ran without genuine FTDI USB serial device'
    pass_message='[H15 NO-USB-SERIAL FAIL-CLOSED PASS] original signed Alpine USB tty userspace refused when required genuine FT232BM/xHCI hardware absent; no fallback'
    bar='[H15 FTDI BAR QUALIFIED]'
    ;;
  h15:no-xhci)
    zero_flags='NAS_QEMU_H15_USB_SERIAL_USERLAND NAS_QEMU_H15_NO_USB_SERIAL NAS_QEMU_H15_NO_XHCI NAS_QEMU_INPUT_OPTIN'
    empty_flags='NAS_VIRTIO_BLK_IMAGE NAS_QEMU_H15_FTDI_SOCKET'
    expected='[H15 FTDI FAIL] required unique QEMU xHCI absent or ambiguous'
    negative_flag=NAS_QEMU_H15_NO_XHCI
    forbidden='H15_USB_SERIAL_READY_FOR_HOST|H15_FTDI_ORIGINAL_BUSYBOX_STTY_PASS|H15_ORIGINAL_ALPINE_BUSYBOX_USB_SERIAL_NONCE_RETURNED|\[H15 USB SERIAL ATTACHED\]|\[EXEC\] replaced process with lwext4 ELF: /bin/busybox|\[M1\] entering actual ring 3|\[EXIT\] user status=0|\[PASS\] cmdline userspace exit 0'
    ambient_message='[H15 NO-XHCI FAIL] ambient serial or block fixture supplied'
    missing_message='[H15 NO-XHCI FAIL] missing explicit guest USB serial hardware rejection'
    hardware_message='[H15 NO-XHCI FAIL] unexpectedly found USB xHCI controller'
    fallback_message='[H15 NO-XHCI FAIL] original signed Alpine ran without genuine FTDI USB serial device'
    pass_message='[H15 NO-XHCI FAIL-CLOSED PASS] original signed Alpine USB tty userspace refused when required genuine FT232BM/xHCI hardware absent; no fallback'
    bar='[H15 FTDI BAR QUALIFIED]'
    ;;
  *) exit 2 ;;
esac
out=build/$profile-qemu-only
image="$out/naseed.iso"
serial_log="$out/$profile-$missing-uefi-serial.log"
clean="$out/$profile-$missing-uefi-clean.log"
test -s "$image" && [ ! -L "$image" ] && [ ! -L "$serial_log" ] || exit 2
# All names originate in the closed profile table above.
for flag in $zero_flags; do
    eval 'value=${'"$flag"':-0}'
    [ "$value" = 0 ] || { echo "$ambient_message" >&2; exit 2; }
done
for flag in $empty_flags; do
    eval 'value=${'"$flag"':-}'
    [ -z "$value" ] || { echo "$ambient_message" >&2; exit 2; }
done
. scripts/acceptance/qemu-negative-observer.sh
label=$(printf '%s %s' "$profile" "$missing" | tr '[:lower:]' '[:upper:]')
case "$profile" in h15) tail_lines=140; stop_tail=120; stop_message="invalid QEMU stop rc=$rc" ;; *) tail_lines=150; stop_tail=150; stop_message="QEMU did not refuse/halt within bound rc=$rc" ;; esac
case "$rc" in 0|1|124|143) ;; *) echo "[$label FAIL] $stop_message" >&2; tail -n "$stop_tail" "$clean" >&2; exit 1 ;; esac
grep -Fq "$expected" "$clean" || { echo "$missing_message" >&2; tail -n "$tail_lines" "$clean" >&2; exit 1; }
pci='[H0 PCI] 00:04.0 vendor=1b36 device=000d class=0c0330'
if [ "$missing" != no-xhci ]; then
    if ! { grep -Fq "$pci" "$clean" && grep -Fq "$bar" "$clean"; }; then
        echo "$hardware_message" >&2
        [ "$profile" = h15 ] || tail -n "$tail_lines" "$clean" >&2
        exit 1
    fi
elif [ "$profile" = h15 ]; then
    if grep -Fq '[H0 PCI] 00:04.0 vendor=1b36 device=000d class=0c0330' "$clean" || grep -Fq "$bar" "$clean"; then
        echo "$hardware_message" >&2; exit 1
    fi
fi
if grep -Eq "$forbidden" "$clean"; then
    echo "$fallback_message" >&2; tail -n "$tail_lines" "$clean" >&2; exit 1
fi
echo "$pass_message"
