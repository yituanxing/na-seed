# Shared kernel, architecture-owned mechanisms

The porting goal is one reusable NaSeed kernel, not separate M0 user kernels.
NaOS (kadds/NaOS) was inspected for its single kernel initialization,
arch/task split, shared ELF load_common and scheduler-policy boundary.
Its x86-specific paging.h is not a multiarch page-table contract.

## Whole-codebase audit; uncompleted integration blockers

- Build/boot: Makefile and kernel/kernel.c use NASEED_PORT_M0 to bypass
  the real init, VM, process, FS and syscall modules for RV64 and ARM64.
  Rework to ONE shared object set plus ISA-specific entry and linker logic.
- MMU: kernel/arch/mmu.h selects x86 only. A semantic VM contract must
  distinguish VA bounds, RWX, mapped frame ownership, root cloning/switching,
  TLB and selectable RV Sv39/48/57 without exposing ISA PTE bits.
- Context/process: kernel/arch/context.h and saved syscall frame are x86.
  Architecture-owned trap/user entry and FPU/TLS are required; one task
  registry, scheduler and process lifecycle must be preserved.
- ABI: kernel/nr.h remains the pinned x86 source, now used only by the
  syscall decoder. All common dispatch/FS/scheduler comparisons use named
  NAS_SC semantic IDs. RV/ARM asm-generic syscall decoding is available;
  raw clone is never treated as fork. stat/dirent/signal packing and the
  architecture trap-to-dispatch adapter are STILL REQUIRED. Unsupported
  calls must return -ENOSYS.
- ELF/exec: share structural validation and program loading through VM,
  keep one transactional exec implementation. This change begins the
  common validator and target ABI metadata; mapping logic not yet unified.
- Serial: separate SBI, semihost and COM1 transports behind a device model.

## Integration gates (NOT yet passed)
1. The SAME kernel/task/process/exec/FS/VM objects must compile and link
   for x86-64, RV64 and AArch64; no port-only userspace substitute.
2. Same source-level workload tested on each target using its native ELF,
   Linux ABI adapter and actual QEMU. The current static-vs-dynamic
   ladder is retained only as tests, not new kernel implementations.
3. Existing x86 authentic Bash/BusyBox and RV/ARM real-QEMU regressions
   must stay green during migration, and page-table mode variants tested.
4. After shared VM is used on RV, retire RV-private segment-map
   orchestration. Never copy filesystem, process or scheduler code.

## This change
- Single validated ELF64 header/PHDR parser used by x86 and RV64.
- ELF machine, musl interpreter, UTS machine and temporary stack/VM
  policy centralised in arch/abi.h for three ISA targets.
- Existing process/exec/loader/filesystem code reuses that descriptor.
- Real workload regressions preserved; host test rejects malformed ELF
  and accepts architecture IDs 62, 243 and 183.

## Linux stat ABI boundary
The SAME common FS fills stat fields. x86-64 uses a 144-byte native
wire struct; RV64/AArch64 use the 128-byte asm-generic struct. The
target ABI owns the wire representation and compile-time size/offset checks;
no per-arch copy of FS logic. Signal/clone ABI is not yet implemented.

## Shared console formatter across three real QEMU architectures
Real x86 COM1, RV SBI DBCN and ARM semihost transports now export only
nas_arch_console_init/putc. The shared drivers/serial.c owns print/decimal/
hex formatting. Existing CRLF behavior remains in the x86 device adapter;
the QEMU boot paths use exactly the same common formatter object.
ARM semihost is *not* misrepresented as an actual PL011 driver.

## Whole-tree ISA build check: no parallel subset hiding common dependencies
The arch/MMU and arch/context selection interfaces now expose shared-kernel
compile-time contracts for RV64 and ARM64; task TLS access is no longer an
x86 FSBASE field access in common code. Compiler verifies ALL non-arch/
and non-platform/ kernel C sources for each target, not only port/M0 files.
This is NOT a full-kernel link: the required real RV/ARM MMU root lifecycle,
trap, task switching, syscall entry and platform exit provider functions are
intentionally unresolved until implemented. Full runtime claims remain gated.

## One shared userland initialization contract
The normal kernel init invokes arch_prepare_user_cpu / arch_enable_user_traps,
rather than executing x86 GDT/TSS/MSR operations from common init. x86
providers still call its original, tested hardware routines. RV/ARM
providers are outstanding real link dependencies, not no-op shims.
CPIO boot program selection now resolves the target musl interpreter from
one architecture ABI descriptor.

## Console integrity detail
Raw nas_putchar is also the program's stdout byte transport. x86
previously emitted CRLF only in diagnostic nas_print, never for individual
user bytes. Shared formatter preserves this split by invoking the
architecture's optional newline hook only for diagnostic nas_print.
No extra CR is injected into BusyBox output or pipe bytes.

## M10: one REAL reusable user address-space implementation (RV64/ARM64)
Both ISA page-table backends retain their existing native PTE formats.
The SAME kernel/mm/user_space.c now owns user page allocation, lookup,
permission changes, eager fork cloning, rollback and frame reclamation;
ISA-specific code provides PTE map/query/unmap/walk/protect and TLB actions.
M0 QEMU kernels link this SAME common source and execute an allocation /
fork-private-frame / mprotect / unmap / reclaim regression with frame counts.
The RV64 real ELF + hardware U-mode path now uses the shared space root
creation and user stack mapping, not a second copy of Limine upper entries.
This is an actual reused runtime implementation, not just headers.

Unfinished: generic VM calls still use the existing x86 provider because
new-ISA full kernel linking/context/syscall trap work is not complete.
mm/mmu_bridge.c exposes the existing nas_mmu_* ABI for later RV/ARM full
kernel linkage; its inclusion in the M0 kernel is deliberately NOT claimed.
The shared ELF mapping needs a final RWX permission pass before W^X;
do not call initial writable mappings a W^X protection guarantee.
