# M9.6 — AArch64 real EL0 entry and synchronous SVC trap

Use the already-tested 39-bit/3-level AArch64 TTBR0 page-table backend
for the first **hardware** ARM userspace test under real QEMU/UEFI/Limine.

- AArch64 assembly constructs a 2048-byte-aligned VBAR_EL1 vector table,
  masks interrupts, prepares MAIR index 0 for 4KiB normal WB user pages,
  configures only TTBR0 TCR fields (T0SZ=25, TG0=4K, EPD0 clear),
  performs TLB barriers, installs TTBR0_EL1 and uses ERET to EL0t.
- Physical frames for one RX code page and one RW user stack page are
  provided by the existing shared allocator; PTEs have real ARM64 AP,
  UXN/PXN, AF and SH fields rather than x86/RISC-V mappings.
- A small isolated AArch64 instruction sequence executes Linux
  asm-generic exit(42) by SVC #0. A real lower-EL synchronous exception
  enters VBAR slot 8 with SP_EL1, and its C handler validates ESR_EL1
  EC=0x15, SPSR_EL1 EL0t, ELR, x8=93, x0=42.
- The x86 BusyBox suite and RV64 external ELF boot remain independent
  regression jobs. CI requires a new **kernel-origin success marker**,
  not only an ELF build or offline descriptor selftest.

Limitations: no external AArch64 ELF yet; raw embedded user instruction
probe only. This is a one-shot hardware trap, not a scheduler, dynamic
loader, signal handler, TLB policy or real PL011 console. QEMU diagnostics
still use semihosting. Unexpected exceptions fail visibly rather than
returning fake Linux success.

Architecture decision exposed by the port: same *semantic* user mapping
test as RISC-V, completely different descriptors and exception mechanism;
therefore no premature shared PTE type or shared CPU register frame.

## Hardware-trap debugging guardrails

First live TTBR0 boot proved the allocator-backed mappings and reported
`[PORT] AArch64 entering live TTBR0 + EL0`, but did not reach the EL0
trap marker. To avoid blind iteration, the vector switch now emits
one final EL1 register-state trace (SPSel, SP_EL1, TCR, MAIR) before
ERET. The descriptor mapping now uses MAIR index 7 instead of rewriting
index 0, preserving Limine's existing memory types for kernel/HHDM.
This remains a QEMU-only diagnostic, not a hardware console.
