# M8.1: common VM policy / x86-64 MMU ownership

NaOS early approach: separate arch page-table implementation from MM syscalls and policy before building optional algorithm providers.

- `arch/x86_64/mmu.c`: existing x86 4-level CR3/PTE walker, root creation, clone/reclaim, 4-KiB mapping/unmapping, protection, user-PA alias and write permission check.
- `arch/mmu.h`: compile-time ISA selection; x86 currently publishes the actual 4-KiB/48-bit user VA constraints. These are deliberately not claimed as universal.
- `mm/vm.c`: original mmap/munmap/brk/mprotect policy and Linux errno return semantics; no raw PTE bit manipulation.
- `m1.c`: ELF, syscall/FD, scheduler stay temporarily, but only use MMU/VM interfaces.
- `scripts/preflight.sh`: check boundary automatically.

No runtime MMU provider selection, LA57, COW, huge-user-leaf support, VMA tracking, SMP/TLB shootdown or user-mode feature claims. Preserve eager private fork, exec rollback and current NX mprotect semantics. Merge only after original M0–M7.5 real BusyBox/Bash/Alpine QEMU regressions pass.
