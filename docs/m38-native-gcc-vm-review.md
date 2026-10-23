# M38 — three-ISA native Alpine GCC: VM ownership and loader review

This review was performed before the cohesive M38 VM change. The repository
has 433 tracked files, including 128 kernel files; all three ISA execution
paths share Task, ELF, FS, syscall dispatch and vm.c, while x86-64 uses its
native PTE walker and RV64/AArch64 reuse user_space.c and native page-tables.

## Actual native failure (not a hypothesized missing syscall)

Stock signed Alpine GCC 15.2 on all three architectures now successfully
execs its *original* cc1 ELF via the shared vfork/execve/dynamic-musl path.
The decisive syscall logs from M38 show:

- cc1 requests MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED with PROT_NONE at a
  low address (e.g. 0x02e6f000 on x86), returning ENOMEM.
- cc1 subsequently tries an ordinary anonymous RW mmap (8192 bytes),
  returning ENOMEM, and reports an allocation failure.
- The original VM ledger admitted MAP_FIXED only if every page had already
  been reserved in the 0x20000000..0x60000000 anonymous mmap window.
  ELF/heap addresses were outside that window, so this was a false
  Linux-compatible rejection.
- Non-fixed allocation maintained one metadata record per allocation,
  with just 64 records, even for contiguous neighboring intervals.

## Cohesive design correction

1. Keep private physical data-frame and PTE ownership in the existing
   arch MMU providers; no architecture-specific Linux syscall implementation.
2. Continue allocating ordinary mmap regions top-down from
   0x20000000..0x60000000. Merge adjacent reservations and allow 128
   non-coalescing intervals per task. Fail explicitly when no metadata fits.
3. Allow Linux MAP_FIXED to replace a user-owned address in the bounded
   lower address domain (from 0x10000 to 0x60000000), including ELF/heap
   pages, while avoiding kernel and the high userspace stack. Stage VMA
   metadata transactionally before changing any page tables.
4. For MAP_FIXED file pages, replace the old page rather than silently
   modifying an old mapping in place. The filesystem still supplies true
   immutable CPIO/volatile tmpfs bytes, and the virtual mapping owns a
   separate physical frame.
5. brk growth cannot overwrite an owned mmap VMA. Shrink removes associated
   VM metadata. fork copies metadata; successful exec resets it; failed
   exec leaves the original map unchanged.
6. Keep missing semantics honest: this first eager VM does NOT claim a
   PROT_NONE mprotect with byte preservation, shared writable mmap,
   COW, demand-page faults or arbitrary MAP_FIXED at the user stack.
   Those require a proper permissions-bearing VMA/page-fault model.

## Gate

Host tests: range coalescing, fixed overlays outside the ordinary window,
interior splits, full registry rollback and the existing VM/ELF ABI tests.
Then run the original three-ISA Alpine GCC+musl cc1->as->ld->hello acceptance,
plus original TinyCC, GNU Make, Lua, BusyBox and full kernel regressions.
Do not merge M38 or delete any older test runtime until the complete gate
has passed. Remove temporary traces only after native GCC has passed.
