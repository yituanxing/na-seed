# M9.3 — AArch64 user page tables driven by real third-ISA differences

On the real UEFI/Limine/QEMU AArch64 M0 boot, run independent *offline*
AArch64 stage-1 TTBR0 page-table tests using the existing physical frame
allocator. Both 39-bit 3-level and 48-bit 4-level 4KiB-granule roots are
built, mapped, translated and reclaimed. These are **hardware-format ARM
descriptors**, not another copy of RISC-V PPN+RWX flags.

The tests match the **semantic** RV64 invariants: map/translate with exact
physical offsets and access rights, refuse destructive remaps and malformed
permissions, reject noncanonical or misaligned addresses, span a 2MiB table
boundary, map the top valid user page, unmap and reclaim all intermediate
page-table memory without freeing caller-owned data frames.

Architectural distinctions:
- Leaf descriptors have ARM AP[7:6], UXN/PXN, AF and SH bits.
- Both intermediate **table** and final 4KiB **page** descriptors use 0b11;
  block descriptors use 0b01, rejected as unsupported instead of recast as
  table pointers.
- User page must be readable; exact execute-only semantics are not promised.
- The offline descriptor sets AttrIndx=0, but MAIR_EL1 index-0 and TTBR0
  installation must be validated before enabling live userspace.
- Changing kernel TTBR1 is not attempted by this offline test.

This is *not* yet a full user ELF/trap/syscall/ASID or TLB maintenance
backend. In particular, the reference root is never installed in TTBR0.
x86 and RV64 test jobs remain unchanged. The design deliberately shares
upper contract (user mapping policy, ownership) while letting each ISA
implement its own descriptor format and supported page-table modes.
