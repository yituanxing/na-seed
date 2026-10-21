# M9.1 — real RISC-V 64-bit multilevel page-table walker

This deliberately builds on the verified Limine/SBI RV64 M0 boot rather than pretending it already enters userspace. The implementation is motivated by the RISC-V privilege specification and NaOS's real `arch/riscv64/mm` structure.

- `arch/riscv64/mmu.c` owns the **actual hardware PTE encoding** and variable-level walking. Sv39, Sv48 and Sv57 use 3, 4, or 5 9-bit directory levels, backed by the existing Limine frame allocator and HHDM.
- The first API maps only 4-KiB low-canonical **user pages**, with explicit R/W/X/U semantics and A/D flags. It rejects misaligned VA/PA, invalid W-without-R, mapped-page overwrites, impossible user VAs, non-table superpage leaves, and zero or oversize physical frame addresses.
- Mapping intermediate tables is transactional on failure: newly allocated page-table frames are rolled back. Unmap coalesces and returns unused intermediate frames. Destroy releases page-table frames only; user data frames stay caller-owned.
- `mmu_test.c` exercises **all 3 modes within the same actual QEMU boot**: mappings spanning a 2-MiB boundary, permissions, translation offsets, invalid page mappings, upper user boundary, unmapping, and full allocator resource accounting. It never swaps the active bootloader's SATP root.
- `scripts/probe-riscv64-m0.sh` requires a new serial marker showing successful **real allocator-backed PTE operations**, in addition to the original RV64 M0 boot marker. x86 userland regression job remains unchanged.

**Limits**: The test is of real page-table memory structures but **not** a live hardware page walk by the MMU. No riscv64 user entry, system call, page fault, COW, TLB shootdown, huge page, kernel half mapping or SATP switch. Those must be implemented and verified in further stages before calling RV64 a fully working kernel.

The next improvement is an architecture-owned, real user/trap frame under `arch/riscv64` together with an isolated user ELF syscall smoke, while preserving x86 regressions.
