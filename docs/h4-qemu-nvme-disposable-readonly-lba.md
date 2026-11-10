# H4: actual NVMe LBA reads through NaSeed's BlockDevice (QEMU only)

H4 builds on the passing H0/H1/H2 safe PCI inventory and H3 QEMU-only
NVMe Admin Identify. It is deliberately a **separate QEMU write-capable
controller test**, not a physical-PC NVMe driver, even though the storage
namespace is read-only. Never boot the H4 ISO on a physical machine.

## Real test path

1. H0–H2 discover the PCI NVMe endpoint; the exact H3 QEMU fw_cfg,
   device identity/BAR0 checks are still required before enabling PCI
   bus mastering or writable NVMe MMIO.
2. `kernel/drivers/nvme/readonly.[ch]` owns one live admin and one I/O
   SQ/CQ, using the existing `nas_dma_qemu_coherent` interface.
3. Identify Namespace ID 1 discovers a nonzero NSZE and its selected
   LBA format. H4 supports 512 or 4096 byte sectors in the protocol; the
   fixed acceptance fixture intentionally uses **512 bytes**.
4. Create I/O CQ/SQ and issue **only NVMe NVM READ opcode 0x02**.
   There is no NVM WRITE, Format, Flush or namespace management command.
5. Register a stable read-only `nas_port_block` object; call
   `nas_block_get`, `nas_block_read` for LBA0 and LBA1, and verify every
   byte against two different markers. `nas_block_write` must return
   `-EROFS` without calling hardware.
6. Release the BlockDevice reference, unregister the port, disable NVMe
   CC.EN and confirm CSTS.RDY=0 **before releasing DMA pages**. If a
   controller cannot quiesce, the DMA pages must remain pinned.

## Strictly disposable fixture

`scripts/check-h4-nvme-seed.sh` generates exactly one 1MiB raw file at
`build/h4-qemu-only/h4-seed.img`, using zeroes plus these bytes:

| Location | Expected bytes |
| --- | --- |
| LBA0 start | `NASEED-H4-NVME-LBA0-20261110` |
| LBA0 byte 511 | `0` |
| LBA1 start | `NASEED-H4-NVME-LBA1-20261110` |
| LBA1 byte 511 | `1` |
| All other bytes | `0x00` |

Expected SHA-256:
`2ec2216d17e0a98425c45853c7cc74846e899b19d78659b4c44df74c632a3207`.

The generator refuses to overwrite an existing unknown file or a symlink;
the QEMU runner forbids ambient block-image overrides and passes the fixed
image with `readonly=on` plus an emulated `nvme-ns,nsid=1`. No host device
or user disk path is accepted. A successful test also checks that this
fixture SHA did not change.

## Reproduce / acceptance

```sh
make hw-h4-host-check
make hw-h4-kernel
make hw-h4-iso
sh scripts/check-h4-nvme-readonly-qemu.sh \
  build/h4-qemu-only/naseed.iso \
  build/h4-qemu-only/h4-qemu-uefi-serial.log
```

The [H4 GitHub Actions workflow](../.github/workflows/h4-qemu-nvme-readonly.yml)
preserves actual UEFI QEMU serial evidence and the isolated ISO + known fixture.
Compilation/host-mock PASS alone is **not** an NVMe READ PASS.

H4 uses a small, original, spec-derived protocol acceptance core only;
it does not copy U-Boot or NaOS GPL driver source. Production reuse still
needs approved upstream SPDX provenance and NaSeed project licensing.

## Not established by H4

- No physical BAR aperture sizing, real PCI resource ownership or hotplug.
- No real IOMMU, DMA mapping, non-coherent synchronization or IRQ/MSI.
- No filesystem or rootfs mount from NVMe, no Linux desktop.
- No real PC or Raspberry Pi boot and no guarantee any physical disk is safe.

The next real-hardware gate needs a selected physical test machine plus
qualified PCI BAR/IRQ/DMA mechanisms. Real-disk writing remains forbidden
until explicit separate authorization and validation.
