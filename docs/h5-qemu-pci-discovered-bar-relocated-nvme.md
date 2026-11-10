# H5: QEMU-only dynamic PCI NVMe resource qualification

## Exact scope

H4 proved the NVMe admin/I/O path can read two sectors through NaSeed's
`nas_block_read()` with byte-for-byte fixture verification. H5 strengthens the
platform binding: the NVMe PCI function is moved from H4's original QEMU PCI
location `00:03.0` to `00:05.0`, while a harmless PCI balloon function occupies
`00:03.0`. The guest must accept the controller using *discovered* PCI BAR
metadata and the explicit QEMU-only resource qualification, not a fixed H4 BDF
or hardcoded BAR address.

The fixture remains a **fixed generated 1 MiB raw image**, attached to QEMU's
NVMe Namespace 1 read-only. H4's deterministic LBA0/LBA1 payload and SHA-256
verification are reused. The controller and guest DMA engine *do* perform
PCI/MMIO writes, but the NVMe NVM WRITE command is never issued.

## Reproduction

```sh
make hw-h5-host-check
make hw-h5-iso
sh scripts/check-h5-pci-nvme-qemu.sh \
  build/h5-qemu-only/naseed.iso \
  build/h5-qemu-only/h5-qemu-uefi-serial.log
```

Acceptance requires the H5 PCI qualification marker, H1/H2 discovery markers,
H4 actual guest LBA0/LBA1 data verification and BlockDevice proof, and the H4
final marker; it fails on guest errors and verifies the generated namespace SHA
both before and after QEMU execution. CI stores the serial log, ISO, and fixed
seed in artifacts.

The first actual H5 QEMU UEFI acceptance on 2026-11-10
([run 42813493844](https://github.com/yituanxing/na-seed/actions/runs/42813493844))
observed BDF `00:05.0`, BAR0 `0x000000c000004000`, and successful exact
LBA0/LBA1 reads through `nas_block_read`. H4 used BAR0 `0x000000c000000000`.
The regression script additionally rejects that old hardcoded BAR0 when
running the H5 fixture, while accepting other valid relocated QEMU BAR values.

## What this does **not** prove

- Relocating the PCI **function** (BDF) does not *generally* guarantee OVMF
  assigns a different physical BAR address. This exact QEMU fixture did show
  a different BAR and now asserts it, but that is not a portable firmware rule.
- Firmware-assigned BAR metadata alone does not establish actual BAR aperture
  size, exclusive ownership, safe DMA address translation, cache coherency,
  interrupt routing, or IOMMU mappings on physical x86 hardware.
- `QEMU` fw_cfg and a known emulated controller identity are acceptance
  restraints, **not** a hardware security boundary against malicious hosts.
- This ISO must **not** be booted on a real computer. Physical-machine support
  remains limited to the separate H0/H1/H2 read-only discovery images.

Driver architecture follows existing NaSeed abstractions, with no GPL source
copied from NaOS or Linux. Real hardware experiments require independently
validated PCI allocation, DMA ownership, abort/quiescence and IOMMU policies.
