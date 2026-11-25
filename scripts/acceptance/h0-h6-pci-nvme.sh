#!/usr/bin/env bash
# H0-H6 single owner for original host/QEMU commands. Each distinct original
# check is executed in its own strict shell so set +e cannot leak across steps.
set -e -o pipefail
p=${1:-}
case "$p" in h0|h1|h2|h3|h4|h5|h6) ;; *) echo "unsupported H0-H6 profile: $p" >&2; exit 2 ;; esac
if [[ "$p" != h0 ]]; then
  ( set -e -o pipefail; sh scripts/ci-install-x86-deps.sh base )
fi
case "$p" in
  h0)
    (
      set -e -o pipefail
      if ! command -v ld.lld >/dev/null 2>&1; then
        sudo apt-get update -qq
        sudo apt-get install -y -qq lld
      fi
      command -v clang
      command -v ld.lld
      command -v cc
    )
    (
      set -e -o pipefail
      make hw-h0-host-check
    )
    (
      set -e -o pipefail
      make hw-h0-kernel
    )
    (
      set -e -o pipefail
      readelf -h build/h0-hardware/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      nm build/h0-hardware/kernel.elf > build/h0-hardware/h0-symbols.txt
      grep -q ' nas_acpi_probe$' build/h0-hardware/h0-symbols.txt
      grep -q ' nas_pci_scan_root$' build/h0-hardware/h0-symbols.txt
      grep -q ' nas_x86_h0_pci_inventory$' build/h0-hardware/h0-symbols.txt
    )
    (
      set -e -o pipefail
      sh scripts/ci-install-x86-deps.sh iso
    )
    (
      set -e -o pipefail
      make hw-h0-iso
      set +e
      timeout 50s sh scripts/run.sh build/h0-hardware/naseed.iso > build/h0-hardware/h0-qemu-uefi-serial.log 2>&1
      rc=$?
      set -e
      if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
        tail -80 build/h0-hardware/h0-qemu-uefi-serial.log
        exit "$rc"
      fi
      tail -80 build/h0-hardware/h0-qemu-uefi-serial.log
      grep -Fq '[H0 ACPI] XSDT=' build/h0-hardware/h0-qemu-uefi-serial.log
      grep -Fq '[H0 PCI]' build/h0-hardware/h0-qemu-uefi-serial.log
      grep -Fq '[H0 COMPLETE] firmware inventory only; no physical disk I/O' build/h0-hardware/h0-qemu-uefi-serial.log
    )
    ;;
  h1)
    (
      set -e -o pipefail
      make hw-h1-host-check
    )
    (
      set -e -o pipefail
      make hw-h1-kernel
      readelf -h build/h1-hardware/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      nm build/h1-hardware/kernel.elf > build/h1-hardware/h1-symbols.txt
      grep -q ' nas_pci_ecam_scan_read32$' build/h1-hardware/h1-symbols.txt
      grep -q ' nas_arch_x86_map_readonly_device_page$' build/h1-hardware/h1-symbols.txt
      grep -q ' nas_x86_h1_ecam_verify$' build/h1-hardware/h1-symbols.txt
    )
    (
      set -e -o pipefail
      make hw-h1-iso
      set +e
      timeout 50s sh scripts/run.sh build/h1-hardware/naseed.iso > build/h1-hardware/h1-qemu-uefi-serial.log 2>&1
      rc=$?
      set -e
      if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
        tail -80 build/h1-hardware/h1-qemu-uefi-serial.log
        exit "$rc"
      fi
      tail -80 build/h1-hardware/h1-qemu-uefi-serial.log
      grep -Fq '[H0 ACPI] XSDT=' build/h1-hardware/h1-qemu-uefi-serial.log
      grep -Fq '[H0 PCI]' build/h1-hardware/h1-qemu-uefi-serial.log
      grep -Fq '[H1 ECAM VERIFIED] CF8/ECAM identical functions=' build/h1-hardware/h1-qemu-uefi-serial.log
      grep -Fq '[H0 COMPLETE] firmware inventory only; no physical disk I/O' build/h1-hardware/h1-qemu-uefi-serial.log
    )
    ;;
  h2)
    (
      set -e -o pipefail
      make hw-h2-host-check
    )
    (
      set -e -o pipefail
      make hw-h2-kernel
      readelf -h build/h2-hardware/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      nm build/h2-hardware/kernel.elf > build/h2-hardware/h2-symbols.txt
      grep -q ' nas_pci_resources_probe$' build/h2-hardware/h2-symbols.txt
      grep -q ' nas_x86_h1_ecam_verify$' build/h2-hardware/h2-symbols.txt
    )
    (
      set -e -o pipefail
      make hw-h2-iso
      set +e
      NAS_QEMU_H2_NVME=1 timeout 50s sh scripts/run.sh build/h2-hardware/naseed.iso > build/h2-hardware/h2-qemu-uefi-serial.log 2>&1
      rc=$?
      set -e
      if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
        tail -90 build/h2-hardware/h2-qemu-uefi-serial.log
        exit "$rc"
      fi
      tail -110 build/h2-hardware/h2-qemu-uefi-serial.log
      grep -Fq '[H1 ECAM VERIFIED]' build/h2-hardware/h2-qemu-uefi-serial.log
      grep -Fq '[H2 PCI RESOURCE VERIFIED] NVMe=1' build/h2-hardware/h2-qemu-uefi-serial.log
      grep -Fq '[H2 NVME CANDIDATE] discovered; no controller writes/DMA' build/h2-hardware/h2-qemu-uefi-serial.log
      grep -Fq '[H0 COMPLETE] firmware inventory only; no physical disk I/O' build/h2-hardware/h2-qemu-uefi-serial.log
    )
    ;;
  h3)
    (
      set -e -o pipefail
      make hw-h3-host-check
    )
    (
      set -e -o pipefail
      cc -std=c11 -Wall -Wextra -Werror -O2 tests/h3-qemu-mmio-host.c -o /tmp/naseed-h3-qemu-mmio-host
      /tmp/naseed-h3-qemu-mmio-host
    )
    (
      set -e -o pipefail
      make hw-h3-kernel
      readelf -h build/h3-qemu-only/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      make hw-h3-iso
    )
    (
      set -e -o pipefail
      sh scripts/check-h3-nvme-identify-qemu.sh build/h3-qemu-only/naseed.iso build/h3-qemu-only/h3-qemu-uefi-serial.log
    )
    ;;
  h4)
    (
      set -e -o pipefail
      make hw-h4-host-check
      make hw-h4-kernel
      readelf -h build/h4-qemu-only/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      make hw-h4-iso
    )
    (
      set -e -o pipefail
      sh scripts/check-h4-nvme-seed.sh
    )
    (
      set -e -o pipefail
      sh scripts/check-h4-nvme-readonly-qemu.sh build/h4-qemu-only/naseed.iso build/h4-qemu-only/h4-qemu-uefi-serial.log
    )
    ;;
  h5)
    (
      set -e -o pipefail
      make hw-h5-host-check
      make hw-h5-kernel
      readelf -h build/h5-qemu-only/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      make hw-h5-iso
    )
    (
      set -e -o pipefail
      sh scripts/check-h4-nvme-seed.sh
    )
    (
      set -e -o pipefail
      sh scripts/check-h5-pci-nvme-qemu.sh build/h5-qemu-only/naseed.iso build/h5-qemu-only/h5-qemu-uefi-serial.log
    )
    ;;
  h6)
    (
      set -e -o pipefail
      make hw-h6-host-check
      make hw-h6-kernel
      make hw-h6-iso
      make hw-h6-ambiguous-kernel
      make hw-h6-ambiguous-iso
      readelf -h build/h6-qemu-only/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
      readelf -h build/h6-qemu-ambiguous/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
    )
    (
      set -e -o pipefail
      sh scripts/check-h4-nvme-seed.sh
    )
    (
      set -e -o pipefail
      sh scripts/check-h6-pci-mmio-qemu.sh
    )
    (
      set -e -o pipefail
      sh scripts/check-h6-nvme-ambiguity-qemu.sh
    )
    ;;
  *) exit 2 ;;
esac
