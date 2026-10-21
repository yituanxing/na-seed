# M11 — Eager anonymous VM lifetime as one system-capability batch

Design driver: NaOS separates VM area ownership (vm::info_t/vm_allocator)
from architecture-specific page tables. NaSeed keeps its existing proven
Linux syscall ABI and eagerly allocated page-table machinery, but now has a
**bounded, per-task anonymous range registry** that owns the *virtual address
space* independently of frame/PTE ownership. No extra kernel or libc.

## Why this is not a single-syscall patch

Previously nas_vm_anon_mmap monotonically subtracted from mmap_cursor and
nas_vm_munmap released physical pages only. Many long-running allocate/free
cycles could exhaust the virtual window even with free frames available.
With no range ownership, interior partial unmaps also could not be reused.

This cohesive stage touches shared Task, VM, exec and build, plus the
controlled-source route's **real binary**:

- Anonymous mmap selects the highest eligible hole in a fixed
  0x20000000..0x60000000 range using per-task nonoverlapping intervals.
  On allocation failure it rolls back already-mapped frames.
- munmap plans all metadata edits before touching any PTE; partial unmap
  may split an interval and freed holes are immediately reusable. When the
  64-range capacity is exhausted, unrepresentable splits fail without
  dropping ownership. A Linux munmap of an unmapped hole still succeeds.
- fork copies VM metadata together with a genuinely private eager-copy
  root. Successful exec clears the previous VMA records only after its
  old root is reclaimed; failed exec preserves original metadata.
- The physical PTE permissions remain architecture-owned. There is
  deliberately no PROT_NONE, MAP_SHARED, COW, lazy pager or file mapping
  claim. x86/RV64/AArch64 use identical lifecycle code.
- Fast host regression exercises gap reuse, partial split, OOM-bound
  metadata, clone isolation and rollback invariants.
- The **same** standard C user program is compiled with native Linux libc
  for all three architectures, executed via Linux or QEMU-user as an
  external oracle and then from actual NaSeed initramfs/QEMU. It runs
  48 x (3-page map / middle hole / remap / mprotect / all-unmap) and eight
  fork/private-write/wait cycles; it must print exactly m11-vm-ok.
- The self-compiled BusyBox source workload grows from 6 to 7 cases; the
  official immutable Debian static 17/17 and Alpine dynamic 7/7 regressions
  remain untouched. The extra test binary is staged **only** for the
  controlled-source route and cannot enter official parity rootfs images.

This is one complete eager-anonymous-VM ownership increment, not a claim
that NaSeed implements all Linux process and VMA semantics. The next
real-workload milestone will decide whether file-backed mappings, COW or
true PROT_NONE justify deeper VMA/pager work.

## Verified first integration failure and common ABI correction

The three original source-built Linux libc binaries executed correctly under
Linux / QEMU-user. The first M11 full-kernel QEMU run accepted all 7/7 on RV64
and ARM64, but x86-64 reported mmap ENOSYS for the standard C program.

Inspection of the **actual unmodified x86-64 glibc program disassembly**
showed `mov $0xffffffff,%r8d` before `__mmap64`: the signed 32-bit
fd=-1 reaches the raw syscall interface zero-extended to
0x00000000ffffffff, not UINT64_MAX. Existing NaSeed mmap erroneously
required fd == UINT64_MAX, even though the Linux MAP_ANONYMOUS ABI **ignores
fd**. This was a shared syscall-contract bug exposed by real user code,
not a reason to special-case the x86 provider or weaken the test.

The complete fix removes the fd restriction only for Linux anonymous mmap;
the unsupported flags/offset policy remains fail-closed. A new host ABI
regression covers both encodings of -1, arbitrary ignored fd values,
invalid mode bits and nonzero offsets on every build. Native QEMU M11
acceptance must be rerun after this change; do not mistake the earlier
x86 failure for a passed result.
