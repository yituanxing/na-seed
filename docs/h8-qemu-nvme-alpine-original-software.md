# H8: original Alpine musl userspace from actual QEMU NVMe ext4 root

H8 extends the independently verified H0–H7 PCI/NVMe path without changing
normal QEMU boot or legacy virtio-blk profiles. The hardware-facing transport
remains an actual emulated Q35 PCI NVMe controller at dynamically qualified
BAR/MMIO, with one fixed **read-only** 128 MiB QEMU namespace. The kernel
mounts its ext4 filesystem at `/` and loads the native Alpine BusyBox ELF
and its genuine musl PT_INTERP from that disk. This is not a CPIO/ramdisk
substitution and is not a physical-machine claim.

## Build, positive and negative acceptance

```sh
make hw-h8-host-check
make hw-h8-iso
sh scripts/check-h8-nvme-alpine-qemu.sh
sh scripts/check-h8-no-nvme-qemu.sh
```

The H8 build uses `OUT=build/h8-qemu-only` and feature flag
`NAS_ENABLE_H8_QEMU_ALPINE_ROOT=1` on top of H0–H7 and lwext4. Its pinned
staging input is the official Alpine 3.23.7 x86_64 release plus the
signature-verified Alpine `runtime` package profile. The checked-in staging
script uses the existing Alpine host pipeline: it validates the release
tarball SHA, installs signed original APKs under Alpine's official target
keyring, verifies ELF architecture and BusyBox's real musl `PT_INTERP`, and
reads back programs from actual ext4 inodes with `debugfs` and `cmp`.

The original Alpine BusyBox, Lua 5.4, jq and SQLite 3 executables, musl loader,
the workload script and a disk-only witness are placed in the external NVMe
image. Limine carries only kernel and a valid **non-executable** rescue CPIO.
The guest boot cmdline is:

```text
naseed.argv=/bin/busybox sh /root/h8-workload.sh
```

The positive CI requires all four application success witnesses in the
**actual QEMU serial stream**, a successful read-only refusal (`mkdir`),
interpreter loaded from NVMe, actual `/` ext4 mount, complete shutdown,
controller release and user status zero. QEMU's `isa-debug-exit` returns
host status 1 for guest status zero. Failures, faults, or virtio-blk fallbacks
invalidate the pass. SHA-256 of the complete read-only backing image and
source binaries are checked before and after QEMU, not just kernel build.

The negative CI boots the identical ISO with **no NVMe controller or disk**.
It requires the explicit NVMe-init failure and forbids every Alpine execution
and mount witness. A CPIO fallback cannot count as H8 success.

## Scope and remaining work

H8 validates native Alpine musl process execution, dynamic ELF loader,
selected POSIX operations and read-only filesystem semantics over genuine
NVMe MMIO/DMA. It does not claim a full Alpine OpenRC login, a writable root,
AHCI/SATA, hot-plug, physical NVMe hardware, or USB. Those are separate
devices and acceptance milestones after H8 is fully green.

The isolated CI workflow is
`.github/workflows/h8-qemu-nvme-alpine-userland.yml`; preserve both positive
and negative serial artifact transcripts for every acceptance decision.
