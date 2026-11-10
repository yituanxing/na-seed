# H7 — original Debian BusyBox + Bash actually executed from QEMU NVMe ext4

## Acceptance contract

H0–H6 only proved firmware/PCI inventory and specific disposable NVMe LBA
reads. Those are **insufficient** to call a storage driver operational for
NaSeed Linux userland. H7's QEMU-only opt-in profile must complete the same
storage-to-software path previously exercised by the VirtIO-Blk/lwext4 tests:

1. QEMU/OVMF finds one uniquely qualified QEMU NVMe PCI function moved to
   `00:05.0`; a harmless device occupies slot 3. Exactly one H7-generated
   `32MiB` ext4 image is exposed by QEMU `readonly=on`.
2. The kernel uses the existing `BlockDevice` read API, with an actual NVMe
   queue+DMA backend, and mounts the on-device superblock at `/mnt` using
   unchanged upstream lwext4 and the shared VFS.
3. The Limine module contains only an emergency Debian BusyBox and a
   control script. **Neither** `/mnt/bin/busybox`, `/mnt/bin/bash`, nor their
   unique disk data can be in the CPIO or another Limine module.
4. The control script launches **two unchanged real Debian package ELFs**
   from `/mnt` through NaSeed `execve`: original pinned Debian static
   BusyBox 1.37.0 (multiple `cat`, `cmp`, `grep`, `ls`, `wc`, `mkdir` applets),
   and original pinned Debian static Bash 5.2.37. Both inspect the unique
   NVMe ext4 data. A `mkdir` against read-only ext4 must fail.
5. User PID1 exits status 0, the filesystem unmounts, controller shutdown is
   proven, and the complete QEMU image SHA-256 remains exactly unchanged.
6. A separate negative actual-QEMU boot with the same ISO but **no** NVMe
   must fail closed and must never run the mounted binaries.

The original Debian package SHA-256s are pinned by
`scripts/busybox-static-fixtures.txt` and `scripts/fetch-debian.sh`.
The fixed H7 test fixture is generated only by
`scripts/stage-h7-nvme-userland.sh`, which refuses to overwrite an
unrecognized preexisting disk; `scripts/run.sh` intentionally refuses any
ambient VirtIO-Blk, async disk, arbitrary raw image or another QEMU NVMe
profile when H7 is selected.

## Repeatable commands

On an Ubuntu environment with its ordinary toolchain, QEMU, OVMF, xorriso,
e2fsprogs, cpio and Debian package tools:

```sh
make hw-h7-host-check
make hw-h7-iso
sh scripts/check-h7-nvme-userland-qemu.sh
sh scripts/check-h7-no-nvme-qemu.sh
```

CI: `.github/workflows/h7-qemu-nvme-original-userland.yml` on the dedicated
feature branch, with uploaded actual UEFI serial transcripts, isolated
kernel/ISO, Debian package provenance and before/after media SHA evidence.

## Safety boundary

This is a **QEMU-only** functional/compatibility gate, *not* a real NVMe
driver qualification. QEMU's fw_cfg signature and known vendor/device,
explicit fixture MMIO window, isolated build flag, read-only QEMU drive,
bounded NVMe queue/DMA, and fail-closed controller selection remain required.
An actual NVMe SSD, physical DMA remapping/IOMMU, surprise removal, PCI BAR
probe/reassignment, runtime IRQs, namespace changes, write/flush support,
power-loss recovery and Raspberry Pi boot have **not** been demonstrated.
Do not boot this write-capable NVMe controller experiment on physical hardware.

H7 is complete only when its own real QEMU serial proof and image hash
verify. Passing a host mock, booting the rescue CPIO, seeing an ext4
superblock, or observing PCI/NVMe Identify alone is **not** the userland gate.
