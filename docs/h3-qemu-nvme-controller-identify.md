# H3: isolated QEMU NVMe admin Identify (write-capable test)

H3 is **not** a physical-PC driver. It extends H0/H1/H2's validated
firmware/ECAM/BAR discovery to a **different, explicitly QEMU-only** build
which attempts one actual NVMe Identify Controller admin command. The H0/H1/H2
UEFI images are still read-only and must stay physically safe.

## Why H3 is separate

"Identify Controller" returns read-only metadata but initiating the command
requires real writes: PCI Command enables MEM decoding and bus mastering;
NVMe CC/AQA/ASQ/ACQ and the submission doorbell change controller state;
the emulator DMA-writes the admin completion and Identify data. It is wrong
to describe H3 as a read-only hardware test. No namespace, backing drive,
LBA read/write, block-device registration, interrupt or userspace attachment
is provided by this fixture.

## Qualification before controller writes

The experimental platform entrypoint is built only with the chained feature
flags H0 + H1 + H2 + `NAS_ENABLE_H3_QEMU_NVME_IDENTIFY=1` into
`build/h3-qemu-only`:

1. Require the QEMU `fw_cfg` signature `QEMU` at x86 ports 0x510/0x511.
2. Require the exact isolated QEMU PCI function `00:03.0`, `1b36:0010`,
   NVMe class/subclass/programming interface `01:08:02`.
3. Require BAR0 to be the known QEMU 64-bit memory BAR at `0xc000000000`;
   no BAR sizing or generic physical address claim is inferred from H2.
4. Map only two BAR pages, supervisor-only, uncached, writable and NX; use
   existing x86 device PTE logic, not a second page-table implementation.
5. Use the existing page-sized `nas_dma_qemu_coherent` provider for three
   admin-only DMA pages. Never present this identity-mapped provider as safe
   for a real IOMMU/non-coherent machine.

If these qualifications fail, H3 does not start the controller. The exact
QEMU BAR base is an acceptance-fixture constraint, not a portable driver.

## Driver boundaries

- `kernel/drivers/nvme/identify.[ch]`: tiny admin Identify protocol core,
  isolated from PCI config and MMU. Controller reset, queue initialization,
  completion-status checks and bounded waits must succeed; DMA pages can be
  released only after the controller is demonstrably quiesced.
- `kernel/platform/x86_h3_nvme.[ch]`: QEMU-only PCI qualification, fw_cfg,
  writable BAR mapping and restoring original PCI Command bits.
- `kernel/platform/x86_h1_ecam.c`: opt-in call after H2 read-only discovery,
  without publishing unqualified `nas_bus_device` resources or binding a
  block driver.
- `scripts/check-h3-nvme-identify-qemu.sh`: actual QEMU UEFI boot, required
  serial success markers and failure-marker scan. A green compilation alone
  is not a successful Identify test.

The admin protocol core does **not** import U-Boot/NaOS driver source while
the project's distribution licensing and upstream SPDX compatibility remain
under review. Reuse existing NaSeed interfaces; later production NVMe
functionality can adapt a license-compatible mature core behind them.

## Reproduction and gates

```sh
make hw-h3-host-check
make hw-h3-kernel
make hw-h3-iso
sh scripts/check-h3-nvme-identify-qemu.sh \
  build/h3-qemu-only/naseed.iso \
  build/h3-qemu-only/h3-qemu-uefi-serial.log
```

The runner adds only `-device nvme,id=naseed_h2_nvme,serial=naseedh2`
through `NAS_QEMU_H2_NVME=1`. It does **not** attach a `-drive` or `nvme-ns`.
The Identify Controller `NN` field is printed as `ControllerNN(raw)`;
it is **not proof of active/attached namespace IDs**. The QEMU fixture
is namespace-free by construction and H3 performs no active-namespace scan.
The dedicated GitHub Action uploads the actual serial log and ISO. Never
flash the H3 ISO onto a physical computer.

## Beyond H3

H4 needs qualified BAR aperture/PCI resource ownership and a real read-only
namespace block path through `nas_port_block`; H5 needs genuine PCI DMA
address translation, IRQ/MSI ownership, IOMMU/coherency and a selected
physical hardware fixture. Do not promote QEMU H3 success to real-hardware
support or a bootable Linux root disk.

References: [QEMU fw_cfg](https://www.qemu.org/docs/master/specs/fw_cfg.html),
[QEMU NVMe](https://www.qemu.org/docs/master/system/devices/nvme.html).
