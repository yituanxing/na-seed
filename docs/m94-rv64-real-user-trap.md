# M9.4 — prove real RISC-V U-mode privilege and syscall entry

This is the first *real CPU-validated* user-mode transition in NaSeed's RV64
port, going beyond allocator-backed offline Sv39/48/57 descriptor checks.

The existing Limine supervisor boot and 3/4/5-level page-table tests run first.
Then a dedicated architectural smoke allocates a fresh root, inherits **only
Limine's kernel-half root entries** and maps newly allocated user code and
stack pages in that root. A tiny actual RISC-V userspace program is copied into
the executable user page and entered with `sret`. The transition writes
`sepc`, `sstatus.SPP=U`, `stvec`, `sscratch`, `satp`, performs
`sfence.vma` and `fence.i`, then runs U-mode machine instructions.

The user code executes `exit(42)` using the RV64 Linux asm-generic ABI
(`a7=93`, `a0=42`, `ecall`). A dedicated **S-mode trap stack** and
architecture entry validates hardware trap cause **8 (U-mode ECALL)**,
`sstatus.SPP=0`, `sepc` within user text, and exact arguments before
printing a required CI success marker. The smoke halts; it never pretends to
return to a functioning user scheduler.

The user root is not freed: it shares bootloader-owned kernel page tables
and the test ends in a controlled halt. This is not general address-space
lifecycle, SMP, nested exceptions, page fault delivery or multi-process
context switching. Those must follow distinct tests.

This stage proves a real privilege boundary and architectural syscall
mechanism, *not* Linux syscall dispatch. Existing x86 BusyBox/Bash QEMU,
RV64 page-table tests and AArch64 MMU tests remain full CI gates.
