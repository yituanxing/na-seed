#!/usr/bin/env bash
# H7-H15 genuine PCI NVMe/AHCI/E1000/USB original Linux QEMU,
# independent profile negatives and immutable upstream ELF receipts.
set -e -o pipefail
p=${1:-}
case "$p" in h7|h8|h9|h10|h11|h12|h13|h14|h15) ;; *) echo "invalid hardware profile: $p" >&2; exit 2 ;; esac
case "$p" in
  h8|h9|h11) (set -e -o pipefail; sh scripts/ci-install-x86-deps.sh alpine) ;;
  h12|h13|h14|h15) (set -e -o pipefail; sh scripts/ci-install-x86-deps.sh alpine-python) ;;
esac
case "$p" in
  h7)
    (
      set -e -o pipefail
      sh scripts/ci-install-x86-deps.sh debian
    )
    (
      set -e -o pipefail
      set -eu
      sh -n scripts/stage-h7-nvme-userland.sh
      sh -n scripts/h7-guest-workload.sh
      sh -n scripts/acceptance/build-qemu-hardware-iso.sh
      sh -n scripts/check-h7-nvme-userland-qemu.sh
      sh -n scripts/acceptance/check-qemu-storage-network-negative.sh
      sh -n scripts/run.sh
      make hw-h7-host-check
    )
    (
      set -e -o pipefail
      set -eu
      make hw-h7-iso
      readelf -h build/h7-qemu-only/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      sha256sum -c build/h7-qemu-only/h7-media-before.sha256
      sha256sum -c build/h7-qemu-only/h7-elfs.sha256
    )
    (
      set -e -o pipefail
      sh scripts/check-h7-nvme-userland-qemu.sh
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-storage-network-negative.sh h7 no-nvme
    )
    ;;
  h8)
    (
      set -e -o pipefail
      set -eu
      sh -n scripts/stage-h8-alpine-nvme.sh
      sh -n scripts/h8-guest-workload.sh
      sh -n scripts/acceptance/build-qemu-hardware-iso.sh
      sh -n scripts/check-h8-nvme-alpine-qemu.sh
      sh -n scripts/acceptance/check-qemu-storage-network-negative.sh
      sh -n scripts/build-lwext4-guest.sh
      sh -n scripts/run.sh
      make hw-h8-host-check
    )
    (
      set -e -o pipefail
      set -eu
      make hw-h8-iso
      readelf -h build/h8-qemu-only/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      (cd build/h8-qemu-only && sha256sum -c h8-media-before.sha256)
      sha256sum -c build/h8-qemu-only/h8-original-alpine-elfs.sha256
    )
    (
      set -e -o pipefail
      sh scripts/check-h8-nvme-alpine-qemu.sh
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-storage-network-negative.sh h8 no-nvme
    )
    ;;
  h9)
    (
      set -e -o pipefail
      set -eu
      sh -n scripts/stage-h9-ahci-alpine.sh
      sh -n scripts/h9-guest-workload.sh
      sh -n scripts/acceptance/build-qemu-hardware-iso.sh
      sh -n scripts/check-h9-ahci-alpine-qemu.sh
      sh -n scripts/acceptance/check-qemu-storage-network-negative.sh
      sh -n scripts/build-lwext4-guest.sh
      sh -n scripts/run.sh
      make hw-h9-host-check
    )
    (
      set -e -o pipefail
      set -eu
      make hw-h9-iso
      readelf -h build/h9-qemu-only/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      (cd build/h9-qemu-only && sha256sum -c h9-media-before.sha256)
      sha256sum -c build/h9-qemu-only/h9-original-alpine-elfs.sha256
    )
    (
      set -e -o pipefail
      sh scripts/check-h9-ahci-alpine-qemu.sh
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-storage-network-negative.sh h9 no-ahci
    )
    ;;
  h10)
    (
      set -e -o pipefail
      sh scripts/ci-install-x86-deps.sh alpine-python-last
    )
    (
      set -e -o pipefail
      set -eu
      sh -n scripts/run.sh
      sh -n scripts/stage-h10-e1000-alpine.sh
      sh -n scripts/h10-guest-e1000-http.sh
      sh -n scripts/acceptance/build-qemu-hardware-iso.sh
      sh -n scripts/check-h10-e1000-qemu.sh
      sh -n scripts/acceptance/check-qemu-storage-network-negative.sh
      make hw-h10-host-check
    )
    (
      set -e -o pipefail
      set -eu
      make hw-h10-iso
      readelf -h build/h10-qemu-only/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      (cd build/h10-qemu-only && sha256sum -c h10-media-before.sha256)
      sha256sum -c build/h10-qemu-only/h10-original-alpine-elfs.sha256
    )
    (
      set -e -o pipefail
      sh scripts/check-h10-e1000-qemu.sh
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-storage-network-negative.sh h10 no-e1000
    )
    ;;
  h11)
    (
      set -e -o pipefail
      set -eu
      sh -n scripts/stage-h11-usb-alpine.sh
      sh -n scripts/h11-guest-workload.sh
      sh -n scripts/acceptance/build-qemu-hardware-iso.sh
      sh -n scripts/check-h11-usb-qemu.sh
      sh -n scripts/acceptance/check-qemu-storage-network-negative.sh
      sh -n scripts/check-h11-usb-host.sh
      sh -n scripts/check-h11-usb-guest-boot-guard.sh
      sh -n scripts/build-lwext4-guest.sh
      sh -n scripts/run.sh
      make hw-h11-host-check
    )
    (
      set -e -o pipefail
      set -eu
      make hw-h11-iso
      sh scripts/check-h11-usb-guest-boot-guard.sh build/h11-qemu-only/lwext4_guest.o
      readelf -h build/h11-qemu-only/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      (cd build/h11-qemu-only && sha256sum -c h11-media-before.sha256)
      sha256sum -c build/h11-qemu-only/h11-original-alpine-elfs.sha256
    )
    (
      set -e -o pipefail
      sh scripts/check-h11-usb-qemu.sh
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-storage-network-negative.sh h11 no-usb-media
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-storage-network-negative.sh h11 no-xhci
    )
    (
      set -e -o pipefail
      set -eu
      (cd build/h11-qemu-only && sha256sum -c h11-media-before.sha256)
      sha256sum -c build/h11-qemu-only/h11-original-alpine-elfs.sha256
    )
    ;;
  h12)
    (
      set -e -o pipefail
      set -eu
      sh -n scripts/acceptance/stage-qemu-usb-alpine.sh
      sh -n scripts/h12-guest-workload.sh
      sh -n scripts/acceptance/build-qemu-hardware-iso.sh
      sh -n scripts/acceptance/check-qemu-usb-hid.sh
      sh -n scripts/acceptance/check-qemu-usb-negative.sh
      sh -n scripts/run.sh
      python3 -m py_compile scripts/acceptance/qmp-hid-evtest.py
      make hw-h12-host-check
    )
    (
      set -e -o pipefail
      set -eu
      make hw-h12-iso
      readelf -h build/h12-qemu-only/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      (cd build/h12-qemu-only && sha256sum -c h12-media-before.sha256)
      sha256sum -c build/h12-qemu-only/h12-original-alpine-elfs.sha256
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-usb-hid.sh h12
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-usb-negative.sh h12 no-usb-kbd
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-usb-negative.sh h12 no-xhci
    )
    (
      set -e -o pipefail
      set -eu
      (cd build/h12-qemu-only && sha256sum -c h12-media-before.sha256)
      sha256sum -c build/h12-qemu-only/h12-original-alpine-elfs.sha256
    )
    ;;
  h13)
    (
      set -e -o pipefail
      set -eu
      sh -n scripts/acceptance/stage-qemu-usb-alpine.sh
      sh -n scripts/h13-guest-workload.sh
      sh -n scripts/acceptance/build-qemu-hardware-iso.sh
      sh -n scripts/acceptance/check-qemu-usb-hid.sh
      sh -n scripts/acceptance/check-qemu-usb-negative.sh
      sh -n scripts/check-host-fixture.sh
      sh -n scripts/run.sh
      python3 -m py_compile scripts/acceptance/qmp-hid-evtest.py
      make hw-h13-host-check
    )
    (
      set -e -o pipefail
      set -eu
      make hw-h13-iso
      readelf -h build/h13-qemu-only/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      (cd build/h13-qemu-only && sha256sum -c h13-media-before.sha256)
      sha256sum -c build/h13-qemu-only/h13-original-alpine-elfs.sha256
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-usb-hid.sh h13
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-usb-negative.sh h13 no-usb-mouse
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-usb-negative.sh h13 no-xhci
    )
    (
      set -e -o pipefail
      set -eu
      (cd build/h13-qemu-only && sha256sum -c h13-media-before.sha256)
      sha256sum -c build/h13-qemu-only/h13-original-alpine-elfs.sha256
    )
    ;;
  h14)
    (
      set -e -o pipefail
      cc -std=c11 -O2 -Wall -Wextra -Werror -fno-builtin -I kernel \
        tests/h14-qemu-hid-shared-host.c kernel/drivers/usb/hid_boot.c \
        kernel/drivers/usb/hid_boot_mouse.c kernel/drivers/input/core.c \
        kernel/drivers/input/evdev.c kernel/mm/heap.c kernel/lib/string.c \
        -o /tmp/naseed-h14-shared
      /tmp/naseed-h14-shared
    )
    (
      set -e -o pipefail
      set -eu
      sh -n scripts/acceptance/stage-qemu-usb-alpine.sh
      sh -n scripts/h14-guest-workload.sh
      sh -n scripts/acceptance/build-qemu-hardware-iso.sh
      sh -n scripts/acceptance/check-qemu-usb-hid.sh
      sh -n scripts/acceptance/check-qemu-usb-negative.sh
      sh -n scripts/check-host-fixture.sh
      sh -n scripts/run.sh
      python3 -m py_compile scripts/acceptance/qmp-hid-evtest.py
      make hw-h14-host-check
    )
    (
      set -e -o pipefail
      set -eu
      make hw-h14-iso
      readelf -h build/h14-qemu-only/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      (cd build/h14-qemu-only && sha256sum -c h14-media-before.sha256)
      sha256sum -c build/h14-qemu-only/h14-original-alpine-elfs.sha256
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-usb-hid.sh h14
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-usb-negative.sh h14 no-usb-kbd
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-usb-negative.sh h14 no-usb-mouse
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-usb-negative.sh h14 no-xhci
    )
    (
      set -e -o pipefail
      set -eu
      (cd build/h14-qemu-only && sha256sum -c h14-media-before.sha256)
      sha256sum -c build/h14-qemu-only/h14-original-alpine-elfs.sha256
    )
    ;;
  h15)
    (
      set -e -o pipefail
      set -eu
      sh -n scripts/acceptance/stage-qemu-usb-alpine.sh
      sh -n scripts/h15-guest-workload.sh
      sh -n scripts/acceptance/build-qemu-hardware-iso.sh
      sh -n scripts/check-h15-usb-serial-qemu.sh
      sh -n scripts/acceptance/check-qemu-usb-negative.sh
      sh -n scripts/run.sh
      python3 -m py_compile scripts/h15-ftdi-socket-oracle.py
      sh -n scripts/check-h15-ftdi-host.sh
      make hw-h14-host-check
      sh scripts/check-h15-ftdi-host.sh
      python3 scripts/check-architecture-boundaries.py --self-test
      python3 scripts/check-architecture-boundaries.py
    )
    (
      set -e -o pipefail
      set -eu
      make hw-h15-iso
      readelf -h build/h15-qemu-only/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      (cd build/h15-qemu-only && sha256sum -c h15-media-before.sha256)
      sha256sum -c build/h15-qemu-only/h15-original-alpine-elfs.sha256
    )
    (
      set -e -o pipefail
      sh scripts/check-h15-usb-serial-qemu.sh
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-usb-negative.sh h15 no-usb-serial
    )
    (
      set -e -o pipefail
      sh scripts/acceptance/check-qemu-usb-negative.sh h15 no-xhci
    )
    (
      set -e -o pipefail
      set -eu
      (cd build/h15-qemu-only && sha256sum -c h15-media-before.sha256)
      sha256sum -c build/h15-qemu-only/h15-original-alpine-elfs.sha256
    )
    ;;
  *) exit 2 ;;
esac
