# M175: end-to-end hardware IRQ into real VirtIO Input

Development line: `naseed-app-batch2`, Draft PR #148. This is an
**architecture-establishment** batch; it does not implement Linux's generic
irqdomain, MSI-X allocation, SMP affinity or a new interrupt worker.

## Why this follows M172–M174

- M172 owns physical device publication and the display provider.
- M173 owns typed PCI/MMIO resource discovery and independently launched GUI
  clients. A class driver does not parse target-specific BAR/interrupt maps.
- M174 owns the mounted `/dev` filesystem, real character `OpenFile` identity,
  and successful 20-original-Alpine-ELF + five-physical-QMP-frame QEMU acceptance
  for x86-64, RV64 and ARM64.
- M175 connects the missing *real interrupt* path to the existing Input Core
  and evdev. M174's mouse and keyboard events previously relied on a bounded
  queue poll at each scheduler boundary; that could not prove any device IRQ.

## Ownership boundaries

1. Platform enumeration knows the physical interrupt wire and fills a typed
   `nas_bus_device.resource.irq` alongside its existing MMIO/PCI resource.
   QEMU ARM virt MMIO slot `i` maps to GIC INTID `48+i`; RV virt maps to PLIC
   source `1+i`. Current Q35 maps the explicitly discovered Input device's
   ICH9 PIRQ E–H to PIC IRQ11 rather than guessing the firmware's line byte.
2. `drivers/core/irq.[ch]` manages device-owned `nas_irq_action` objects,
   shared IRQ dispatch, first-owner enable and last-owner mask. There is no
   second device registry or heap-backed ISR registration. Controller
   recognition, target configuration, claim and EOI remain platform-owned.
3. The common VirtIO transport owns device-side interrupt confirmation.
   MMIO reads `InterruptStatus` (offset 0x60) then writes `InterruptACK`
   (0x64); modern PCI reads its mandatory ISR_CFG capability type 3, which
   clears INTx in hardware. Each action checks **its own** source when the
   controller reports a potentially shared wire. Q35's polled block/network
   transports register an ACK-only action if they share an enabled Input PIRQ.
4. The Input class ISR does nothing beyond source ACK and an atomic pending
   flag. Existing DMA queues, Input Core, evdev, VFS and graphical clients
   remain unchanged. `scheduler_service` drains the existing bounded queue
   at a safe boundary, not on an interrupt stack; quota exhaustion keeps
   pending work for a later service slice. No extra permanent kthread is
   consumed from the kernel's four-worker pool. Initialization explicitly
   clears stale device ISR state and binds the controller **before** publishing
   event buffers: QEMU ARM virt declares edge-triggered GIC SPIs, so enabling
   the route only after the device has asserted a held-high status risks
   losing a later rising edge. This ordering is shared across all three ISAs.
5. Driver removal refuses to free in-use Input devices; once safe, it unbinds
   the IRQ action before stopping/resetting transport and freeing DMA.
   Hardware-routed devices must not silently fall back to polling if IRQ
   registration or source ACK is unavailable. Unrouted historical host mocks
   retain their original explicit polling behavior.

## Architecture traps and safety

- ARM64: GIC PPI27 remains the only timer/preemption interrupt. Explicitly
  enabled SPI48–79 device IRQs are dispatched and acknowledged before EOIR;
  both interrupted EL0 and EL1h idle restore their own original frames.
- RV64: the existing OpenSBI STIP timer remains; PLIC S-mode hart0 source
  claim/complete adds SEIP cause 9 in both user and idle traps. The device
  path does not advance `sepc` or run timer preemption.
- x86-64: the existing PIT IRQ0 remains at vector 0x20. Q35 routes selected
  ICH9 PIRQ to PIC IRQ11/vector 0x2b, unmasks PIC cascade, uses level trigger,
  reads shared PCI ISR sources, and sends slave then master EOI. The separate
  ISR preserves all GPRs and native kernel/user IRET frames, without treating
  device input as a timer tick.

## Acceptance and limits

- `sh scripts/check-m175-irq-core.sh` compiles and executes production IRQ
  shared-action lifetime/rollback tests, modern PCI ISR cap tests, two actual
  class-driver queues consuming 12+12 host-injected events across the 8-event
  quota, no-IRQ/no-speculative-poll checks, and 76 shared-core/ISA boundaries.
- The exact-head `.github/workflows/m175-irq.yml` host gate also retains
  M166 input and M174 devfs regression coverage. It is the **one** active
  native three-ISA CI batch; M174 remains available through manual dispatch.
- `sh scripts/probe-m175-irq.sh amd64|riscv64|arm64` retains M174's 20 signed
  original Alpine ELF entries, executable function checks, and M173's five
  independent real QMP framebuffer images. The extended QMP input oracle
  sends keyboard/tablet bursts before the ordinary GUI interactions. Guest
  reports `keyboard_ack`, `pointer_ack`, deferred drain counts, each device's
  delivered event count and fallback polls. The gate requires both devices'
  IRQ ACK/drain to be nonzero, each to consume more than eight records, and
  `fallback_poll=0`, together with the physical pixel transitions.

The host simulation cannot establish hardware interrupt delivery. Only a
successful **exact pushed head** x86-64/RV64/AArch64 native-QEMU matrix may
be reported as M175 IRQ success. Current platform choices apply to the pinned
QEMU virtual machines and one vCPU; broader physical discovery, hotplug and
MSI-X remain later, independently motivated architecture work.
