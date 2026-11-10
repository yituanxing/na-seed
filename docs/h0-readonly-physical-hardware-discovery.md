# H0: read-only physical hardware discovery (x86-64)

This is the first NaSeed hardware bring-up gate, **not** an assertion that
NaSeed already boots a physical PC or Raspberry Pi. It branches from the
existing `naseed-app-batch2` Draft PR without changing normal QEMU boot.

## Build and verify

```sh
make hw-h0-host-check
make hw-h0-kernel
make hw-h0-iso
```

The isolated image is `build/h0-hardware/naseed.iso`. Never reuse the normal
`build/` object files for the H0 compile flag. The image uses the ordinary
Limine x86-64 UEFI handoff and stops before Task/user-mode init, VirtIO,
disk mounting, networking, IRQ setup, or writable platform reset.

The H0 GitHub workflow also compiles the complete x86-64 kernel, creates an
isolated UEFI ISO, and boots it with OVMF under QEMU **without a writable
disk**. Only `[H0 ACPI]`, `[H0 PCI]`, and `[H0 COMPLETE]` serial evidence
can pass this smoke gate. This is an emulated firmware handoff test; it is
not evidence of a physical-machine boot.

## Physical test policy

1. Boot only on a spare test PC from a removable boot device. Do not flash
   this image to a Windows/system disk, alter existing partitions or write
   to any internal drive. Do not enable experimental device drivers yet.
2. Record machine/firmware model, kernel commit SHA, photograph or COM1
   serial log, and the final `[H0 COMPLETE]` line. A connected COM1 can
   collect early text; when firmware exposes a supported 32-bit Limine GOP
   framebuffer, the H0 build also mirrors diagnostics to an 80x25 screen.
   A USB-to-serial adapter plugged into the *target* PC is not a COM1 port.
3. Verify RSDP/XSDT/RSDT/MCFG/MADT are shown as discovery **only**. The
   strict ACPI reader checks signed table lengths, checksums and that all
   physical table reads belong to Limine ACPI-mapped memory. Bad firmware
   tables fail closed and do not trigger speculative MMIO reads.
4. The optional legacy x86 PCI config reader probes segment 0 from root
   bus 0 through configured PCI bridges. It writes only CF8's selection
   address and reads CFC; it never writes device configuration, touches
   BARs, enables bus mastering or binds drivers. It does not replace a
   future generic ACPI MCFG/ECAM host bridge implementation.
5. An `[H0 COMPLETE]` marker proves **only** that read-only diagnostics
   reached a bounded halt. It does not prove NVMe, AHCI, USB, networking,
   Alpine, Debian, or a physical machine is functional.

## Next driver gate

From NaOS's Raspberry Pi and PC history, the reusable boundary is:

`firmware/FDT -> host bridge -> nas_bus_device -> IRQ/DMA -> driver -> nas_port_block/nas_netdev -> lwext4/lwIP`

Implement platform-resource mapping and ACPI MCFG/IOAPIC/MSI as separate
providers, then use an existing, license-audited NVMe/USB implementation
behind the existing block and network class ports. No second VFS, socket,
or block registry is permitted. For real disk writes, use only a dedicated
disposable test disk and separately verify flush, reboot readback and fsck.
