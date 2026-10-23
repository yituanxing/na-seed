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

## Real GCC assembler: readv as a shared Linux FD operation

After the VM batch, cc1 compiled successfully on x86 and ARM64 and began
executing upstream GNU binutils as. It failed with ENOSYS from readv
(x86-64 syscall 19; RV64/AArch64 generic syscall 65). This is not an
assembler issue. Provide one vectored-read Linux ABI over the existing
open-file-description offsets, VFS reader and Task pipe/TTY replay. Add
a second unmodified-musl userspace program to the GCC acceptance gate
that tests readv contents, EOF, and ordered offset advancement.

## Stable end-to-end acceptance harness

The first follow-on readv workload made Limine's small boot-time
`naseed.argv` staging buffer exceed 256 bytes. Do not change the kernel
argument parser to support an acceptance-only command string. Stage one
real POSIX shell script into the original Alpine CPIO and run it via
stock BusyBox ash. This matches real OS boot and userspace behavior:
process argv and compiler subprogram invocations come from libc/shell,
not a bootloader test harness. Both the original GCC hello and the
new readv test remain mandatory markers.

## Signed Alpine package post-install behavior

Foreign architecture apk installs deliberately use --no-scripts. Alpine's
BusyBox post-install trigger normally creates /bin/sh, but scripts cannot
run on the host for a foreign ISA. Stage that exact standard BusyBox link
in the immutable rootfs, verify that it resolves to the authentic package
binary, and continue executing stock sh rather than using an OS-side
special-case or substituting a handcrafted shell.

## Linux filesystem creation mask and ABI acceptance

Native original GCC+musl compiles and runs hello on all three ISAs, but
its second GCC-built readv executable initially lacked its execute bit.
An independent missing syscall appeared: umask (#95 x86; #166 RV64/ARM64).
Implement one per-Task Linux umask (default 0022), inherited across
fork/vfork, unchanged across exec, and clear on task slot reclamation.
The native open/openat mode argument now reaches tmpfs inode creation,
as does mkdir, with the process mask applied exactly once. chmod/fchmod
remain mask-independent and execve still enforces actual inode X bits.
A third GCC-built libc program verifies default mask, fstat/stat,
fchmod/chmod, mkdir, per-child mask inheritance and parent isolation;
require its marker alongside hello/readv on all three QEMU guests.
