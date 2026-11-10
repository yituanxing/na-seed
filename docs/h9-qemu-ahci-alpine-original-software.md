# H9: QEMU ICH9 AHCI/SATA read-only ext4 with genuine Alpine programs

H9 changes only the physical disk transport relative to the verified H8
QEMU NVMe milestone: the same official Alpine 3.23 release's signed APK
`musl`, `busybox`, `lua5.4`, `jq`, and `sqlite` ELF bytes become the
application workload on a genuinely emulated **SATA** disk attached to the
Q35 chipset's built-in ICH9 AHCI PCI function (normally `00:1f.2`). This is
not an image containing Linux host-built substitutes, a CPIO execution
shortcut, a physical SATA driver, or a full Alpine/OpenRC login claim.

The AHCI core is restricted to ATA IDENTIFY DEVICE and READ DMA EXT. It has
one polling command slot, dedicated DMA command-list, received-FIS,
command-table and bounce-page leases, and must stop both port engines and
confirm their hardware-running bits clear before freeing DMA pages. No SATA
write/flush operation is supported. PCI BAR5 is qualified, mapped UC and
claimed only for the known QEMU profile. The exact host image is a bounded
128 MiB raw fixture with mode `0444` and SHA-256 witness before and after.

QEMU's `ide-hd` frontend refuses a block node with `readonly=on`; this is
different from NVMe's support for a read-only QEMU backing node. To avoid
unblocking real media writes, H9 uses `snapshot=on` **only on its fixed SATA
drive**: QEMU redirects any hypothetical writes into a disposable temporary
overlay; the raw `0444` backing image is never written. NaSeed's SATA driver
and VFS additionally expose readonly semantics and issue no ATA writes.
The image's unchanged SHA-256 is still required after actual guest execution.

## Exact run and acceptance contract

```sh
make hw-h9-host-check
make hw-h9-iso
sh scripts/check-h9-ahci-alpine-qemu.sh
sh scripts/check-h9-no-ahci-qemu.sh
```

Only the H9 profile enables `NAS_ENABLE_H9_QEMU_AHCI_USERLAND=1`; it requires
H0/H1/H2 PCI inventory plus lwext4, **not** NVMe or VirtIO-Blk. Separate
`OUT=build/h9-qemu-only` objects, ISO and media prevent cross-contamination.
The existing `scripts/stage-h8-alpine-nvme.sh` signs and checks the official
Alpine source pipeline; H9 independently copies those exact package bytes to
its own ext4 medium, checks real inodes with `debugfs`, and uses a different
on-disk H9 script and data marker. Its Limine rescue CPIO contains metadata,
no executable and no musl interpreter. Selected PID1 arguments are:

```text
naseed.argv=/bin/busybox sh /root/h9-workload.sh
```

The positive **actual QEMU/OVMF** guest must show Q35 AHCI qualification,
`ahci0` block registration, readonly ext4 at `/`, the original dynamic
BusyBox and musl PT_INTERP read from SATA media, successful BusyBox/Lua/jq/
SQLite operations, refusal to create a file on readonly media, PID1 exit 0,
real upstream ext4 unmount, verified AHCI DMA quiescence and resource release.
The complete 128 MiB disk SHA-256 must remain unchanged. Its `isa-debug-exit`
host status `1` corresponds to guest zero; merely compiling or printing a
mock message is never sufficient.

The independent negative boots the **same** ISO without any SATA hard disk.
Q35's default AHCI controller and the ISO optical device still exist; those
may not fake a SATA ATA disk or Alpine ELF source. It must reject the absent
disk before userland, with no CPIO/NVMe/VirtIO fallback. Both serial logs are
saved by `.github/workflows/h9-qemu-ahci-alpine-userland.yml`.

## Safety and scope

This remains **QEMU-only** hardware testing. The port's write-capable MMIO,
PCI bus mastering and DMA are never enabled on an arbitrary physical PC.
The QEMU coherent DMA provider does not provide real IOMMU isolation or prove
non-coherent physical hardware correctness. Interrupt-driven SATA,
hotplug, AHCI writes and true-machine qualification are deferred. H8 original
NVMe software-boot success is a prerequisite and remains independently
verified, not silently replaced by H9.
