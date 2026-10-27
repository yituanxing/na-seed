# Mature C driver port — source audit and live NaSeed boundary

## Evidence and selection (2026-10-27)
- NaOS history: `98fa26a2` imported upstream lwext4; its adapter used
  global VFS handles and ignored real I/O errors. Do not copy that adapter.
- NaOS `b6f92e7e` deleted its rcore-os Rust VirtIO driver; `f6390dde`
  introduced a C VirtIO-Blk. Current NaOS VirtIO core is split into
  bus, queue, PCI, MMIO, and a separate block module.
- NaOS's current C VirtIO code is under GPL-3.0; it is a *reference*, not
  an approved drop-in dependency for NaSeed's as-yet-unresolved project license.
- Zephyr (Apache-2.0): `drivers/disk/virtio_blk.c` plus VirtIO PCI/MMIO
  transports is the preferred C upstream to EVALUATE for a pinned transplant.
  It depends on Zephyr device-tree, disk, DMA, kernel scheduling, semaphore,
  and interrupt APIs. Do not blindly copy one file and claim it runs.
- Apache NuttX (Apache-2.0): `drivers/virtio/virtio-blk.c`, framework and
  transport depend on OpenAMP and NuttX block/interrupt/memory facilities.
  Keep as a second reference, not an extra kernel-wide compatibility layer.
- Unikraft (BSD-3-Clause default, check each file): mature VirtIO block
  implementation, but coupled to ukbus, ukblkdev and memory allocation.
- rcore-os/virtio-drivers (MIT): explicitly no_std **Rust**, not C;
  do not introduce a second kernel language just for a storage proof of concept.

## Non-negotiable NaSeed boundaries
```
  Linux syscall ABI -> VFS -> lwext4 -> nas_block_device (unchanged)
                                                ^
                                  kernel/drivers/port/block.[ch]
                                                ^
                    pinned third-party C driver (NuttX sector-count or
                    Zephyr zero-on-success callback convention)
                                                ^
                                its native VirtIO bus/queue/PCI/MMIO
                                                ^
                      NaSeed frame/DMA, MMIO maps, PCI, IRQ adapters
```

- `nas_port_block_attach` is a working C adapter, not a second block layer.
  Driver-owned lifetime-stable storage, mandatory write flush, exact-count
  enforcement, error propagation, and existing BlockDevice mount refs apply.
- `NAS_PORT_BLOCK_SECTORS` translates sector-count successful returns.
  `NAS_PORT_BLOCK_ZERO` translates zero-on-success callbacks.
- The *existing real AArch64 modern VirtIO-MMIO driver* uses the adapter
  and retains the exact hardware-backed persistent ext4 QEMU acceptance.
  This is a **temporary fallback** while a pinned C upstream is integrated.
- No vendored Zephyr/NuttX code was added by this change. This explicitly
  does NOT claim an upstream driver is yet running in NaSeed.

## Next transplant acceptance gate
1. Pin a precise upstream commit and preserve upstream file headers, NOTICE
   and licensing; resolve NaSeed combined-work license before importing.
2. Bring the smallest coherent driver implementation with its queue/core
   dependencies; keep foreign files intact in `third_party/`, translations
   and NaSeed-native interfaces under `ports/`. Avoid full Zephyr or
   NuttX runtime shims in the core kernel.
3. Map DMA and cache/coherency, IRQ completion, PCI ECAM and MMIO device
   discovery through architecture-neutral platform hooks. Return ENOTSUP
   for unsupported capabilities, never fake durable FLUSH.
4. Replace the fallback **only after** actual AArch64 physical-image
   write+flush+e2fsck, then RV64 and x86-64 PCI, passing the same
   exact-head native application and filesystem CI. No merge before that.

## First real upstream code in the AArch64 driver

Pinned commit `359307a2a4cc9c1feb22683d841650902b799e4d` provides byte-for-byte imported upstream
`virtio_ring.h` and `virtio_types.h` (BSD-3-Clause with original notices).
The existing real QEMU driver now constructs and uses their vring layouts
via the original `vring_init` and `vring_size` helpers; its local
duplicate descriptor/ring type declarations and magic queue offsets are gone.
The upstream blob Git SHAs are verified by CI. Architecture typedefs remain
in a separate NaSeed port shim; lwext4 and VFS interfaces are unchanged.

This stage is explicitly **upstream queue ABI adoption**, not a full
Unikraft `virtio_blk.c` transplant. The upstream full driver currently
depends on `ukblkdev`, `uksglist`, VirtIO bus, PCI/MMIO and other uk APIs;
a full source-port must preserve original algorithms and only adapt
platform hooks in `ports/unikraft`. Do not claim the source is fully
integrated until it links and passes the same persistent QEMU guest tests.

## Cross-driver bus binding and DMA boundary (second implementation batch)

The kernel now owns a **single** `nas_bus_device`/`nas_bus_driver`
registry in `kernel/drivers/core/bus.[ch]`. Transport/platform discovery
publishes persistent device descriptors, drivers register match/probe and
optional removal, and the bus binds them. Unmatched device types stay
available for late driver registration. No VFS calls or syscall knowledge
enter the bus. Busy mounts prevent BlockDevice removal; bus unbind
also respects driver-controlled removal. Hot removal of the initial legacy
VirtIO-MMIO fallback is deliberately unsupported (no .remove), never
pretends to have safely detached DMA.

The first live consumer is **AArch64 QEMU VirtIO-MMIO**: the platform
enumerates valid modern MMIO slots, registers generic bus devices, and
the VirtIO-Blk driver binds type=2. A second VirtIO device can be
registered later without extending the platform's if/else chain.
`NAS_BUS_PCI` is declared for future physical PCI discovery but is
**not** evidence that PCI probing works today.

The DMA API `kernel/drivers/port/dma.[ch]` makes device DMA address,
CPU alias, allocation, synchronization and release separate from the driver.
The initial provider explicitly declares QEMU **identity, coherent** memory.
Unprovided noncoherent sync fails closed; a future real-hardware provider
must supply cache maintenance and, if necessary, IOMMU mappings.
The genuine VirtIO submit path allocates three pages through this API,
programs bus addresses and synchronizes ownership at request boundaries.

Host regression tests drive this bus with a second synthetic driver,
verify late binding, failed busy unbind and explicit removal. DMA tests
verify allocation/release/bounds and refusal of unsupported noncoherent
sync. The full original AArch64 physical QEMU disk write, FLUSH, and
host e2fsck remain the acceptance gate. No new third-party full driver
has been claimed.
