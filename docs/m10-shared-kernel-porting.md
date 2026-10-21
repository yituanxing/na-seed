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

## Shared executable load — no second PT_LOAD algorithm
The x86 native full loader and RV64 real U-mode test now call the SAME
kernel/task/elf_segments.c for whole-image checks, executable entry,
PT_LOAD mapping, file copying and zero-filled BSS. RV-private ELF loader is
only a VM callback adapter; no duplicate structural/segment loops.
AArch64 compiles the same shared segment engine. Native executable choices
remain per target (x86 62, RV64 243, ARM64 183), with dynamic musl selection
in the existing full exec path. RV64 static smoke rejects PT_DYNAMIC/INTERP,
overlapping pages, W+X and malformed alignment. Common frame allocator
zeroes BSS. Verified by a host loader regression and real RV QEMU.
Current x86 MMU map_page still does not guarantee runtime W^X: separate
leaf-protection finishing passes are a different feature, not claimed here.

## Page-table shape is not interchangeable between ISAs
RV64 SVx leaf walker iterates 256 top-level positive-canonical entries
because upper indices hold shared Limine supervisor mappings. ARM64 TTBR0
has a separate root from TTBR1 and ALL 512 entries of TTBR0 are userspace.
The shared VM regression now maps, clones and reclaims the highest legal
user VA with hardware-selected Sv39/48/57 or TTBR0 39/48-bit semantics.
This detects silent unreclaimed user frames and private-fork omissions.

## CI evidence policy
The RV64 and ARM64 real-QEMU probe scripts require explicit PASS markers
from the same linked kernel/mm/user_space.c regression after high-user-VA
fork-private mappings are checked, not just older architecture smoke markers.

## Real RISC-V trap agrees with the SAME Linux syscall ABI
The already-verified RV64 hardware ECALL from the native ELF now decodes
its a7 Linux syscall number with arch/linux_syscall_abi.h, exactly as the
full generic syscall dispatcher does. The smoke trap still asserts
exit(42); this is evidence of shared ABI interpretation, NOT a second
syscall/process implementation or a complete RV64 syscall entry yet.

## Real-QEMU CI timing without dropping correctness
Before M10, each RV/ARM QEMU boot always spent 90s in timeout because the
guest halts after its PASS markers. Both architecture probe scripts now
monitor the real kernel's serial log and terminate QEMU only after ALL
required markers (including shared VM high-VA clone/reclaim) appear and no
FAIL marker is present. An incomplete or failed boot remains a failure.
Separate POSIX host regression verifies no partial-marker acceptance.

## Retiring the RISC-V-only ELF frontend
The old kernel/arch/riscv64/elf_image.{c,h} has now been deleted, not
left as a second application loader. kernel/task/elf_user.c is a
shared user address-space adapter and links into **both** RV64 and ARM64
real boot images. The RV64 test passes only its workload bounds into
that common adapter, which calls the same kernel/task/elf_segments.c as
full x86 execve. No ISA parser or PT_LOAD loop remains under arch/riscv64.

## Linux native BusyBox fork ABI (shared task path)
On x86 libc can invoke fork(57). On RV64/AArch64 the Linux asm-generic
interface typically invokes clone(220, SIGCHLD, 0), not a fork syscall.
The ISA decoder now normalizes ONLY exactly fork-equivalent clone flags
(SIGCHLD with null child stack) to NAS_SC_fork; the full kernel continues
to call the SAME nas_process_fork, scheduler, MMU and FD implementations.
Threads and CLONE_VM are correctly unsupported (-ENOSYS), not silently
converted into a bogus fork. Host regression tests both accepted and
rejected clone forms. Live non-x86 trap/context entry is still incomplete.

## RV64 full-kernel BusyBox integration gate (not a separate OS)
- Build kernel from the SAME generic core .c files as x86, plus real RV64
  CPU, trap, context, MMU, console and platform provider files. This has
  **no NASEED_PORT_M0**. ELF/task/syscall/fs/pipe/signal/process/exec/VM
  are direct original shared sources. No duplicate kernel implementation.
- RV64 supervisor ECALL frame saves all 31 GPRs, sepc and sstatus, then
  translates Linux a7/a0..a5 to the existing x86-neutral dispatcher format.
  Scheduler uses real saved per-task RV contexts and per-task hardware root.
- Real user floating-point ABI requires the D/F ISA; full-kernel path saves
  and restores f0..f31 plus fcsr on task switches. Unsupported CPUs fail.
- First runtime acceptance is a real linked native RV64 ELF which exercises
  the same common boot image, ELF loader, Linux stack, UART write syscall
  and syscall exit path as x86. That is a prerequisite, **not BusyBox parity**.
  Next step is native Debian/Alpine RV64 BusyBox and identical x86 test cases.
- CI explicitly checks full shared-kernel QEMU path in addition to existing
  M0 hardware smoke (and x86 suite); never conflate these two tests.

## Native RV64 BusyBox — same workload source as x86
The full RV64 kernel now links all x86 generic task/exec/scheduler/FS/VM/
syscall objects and passes real ELF write(1) + exit(0) using that one path.
Next CI target fetches the **unmodified official Debian 13 riscv64
busybox-static 1.37.0-6+b9** package and SHA256-verifies its native ELF,
rather than recompiling/copying a test-only BusyBox. The same native
kernel boots it as a Limine module using the existing common ELF loader,
Linux stack and syscall dispatcher. Acceptance requires BusyBox's own
stdout hello plus successful exit, identical to the x86 BusyBox smoke.
The full x86 test *matrix* (CPIO ash, fork/pipe/tmpfs, etc.) remains a
separate further gate and must not be declared passed prematurely.

## RV64 parity matrix rather than a one-off echo
scripts/probe-riscv64-busybox-matrix.sh now runs 12 QEMU-booted native
BusyBox applets through the **same full task/ELF/VM/FS/syscall core**:
six direct-module tests (echo, uname, pwd, id, true, printf), plus six
*archive-only CPIO* cases (ls root/bin, cat, head, grep, wc).
Each asserts native userspace exit 0, correct Limine argv, and exact
stdout. Archive-only checks real bootfs mount, not a pretend in-kernel
file response. This matrix is required in RV64 CI beside the existing
native Debian BusyBox echo and low-level M0 tests.
Longer x86 ash/fork/pipe/tmpfs workflows remain separate remaining gates.

## Real RV64 ash compatibility gate
After 12 verified native BusyBox direct/CPIO workloads, five further
real original-BusyBox ash cases exercise shared kernel shell startup,
execve process replacement, fork/exec/wait4, pipe2/pipeline/EOF and tmpfs
create/remove. Assertions check actual serial stdout, Linux exit status,
and kernel process/pipe/exec evidence. Batch all five failures into one CI
feedback cycle to expose a whole error class instead of one test per PR.
Complete x86 resource/signal/large-pipe/dynamic ELF parity remains pending.

## RV64 native shell clone ABI blocker (measured, not papered over)
The original Debian RV64 BusyBox passed 12 full-kernel direct/CPIO applets
and shell builtins + an exec-builtin image replacement. Three additional
ash fork/pipeline/tmpfs workflows all emitted a native Linux clone(220)
request rejected by the intentionally strict fork-equivalent decoder.
Rather than silently mapping thread/vfork semantics to a fake fork, the
shared dispatcher now records actual RV64 clone flags, child stack and
TID/TLS arguments. Implement only the measured semantics, sharing the
existing scheduler, address space, FD and process lifecycle. This blocker
must stay a failing real CI gate until correctly handled.

## Native Debian RISC-V BusyBox ash — measured clone contract
CI QEMU logs of three independent ash failures all identified exactly:
Linux clone(220), flags=0x01200011 (SIGCHLD|CLONE_CHILD_SETTID|
CLONE_CHILD_CLEARTID), child stack=0, parent TID=0, TLS=0, child TID
address=0x1a1d30. This is a **separate-VM process fork** with child
TID bookkeeping, NOT vfork, shared-VM threads or generic clone.

The ABI decoder now accepts ONLY that measured form as semantic
NAS_SC_fork_child_tid and routes it to the SAME process_fork core.
The fork core writes the child PID as a 4-byte Linux tid_t in the CHILD's
copy of memory, leaves parent memory unchanged, and registers an exit
clear_child_tid action performed before frame reclamation; fork without
those flags still uses the exact prior algorithm. Unsupported clone
flags/alternate stacks fail -ENOSYS. Thread-shared futex wake remains
unsupported because CLONE_VM/CLONE_THREAD are not implemented.
This is a real ABI contract, not returning success without behavior.

## RV64 first post-fork hardware fault — context snapshot ordering
The native ash fork/pipeline/tmpfs cases reached actual child creation
and child TID setup, then faulted at the identical U-mode instruction
PC=0x102d86 with scause=15 (store/AMO page fault). Whole-system review
found a concrete common-core bug: nas_task_fork_slot() copied the parent
struct BEFORE nas_arch_save_user_frame(parent), leaving the child with a
stale/zero saved RV64 user TP. nas_arch_restore_cpu(child) then restored
that stale TP, so libc's post-clone thread-pointer-relative store faulted.
Fix in ONE common process fork: snapshot live user CPU and FP state
before copying the child task. Preserve the existing child-set/clear-TID
semantics. Additional fault diagnostics report stval/tp/sp to confirm
real page address if the hardware fault recurs. Do not declare shell
regressions passed until real QEMU verifies.

## BusyBox test provenance is fixed centrally (no ISA-local choices)
`scripts/busybox-static-fixtures.txt` pins Debian 13 BusyBox **1.37.0**
with EXACT binary package version `1.37.0-6+b9` for both amd64 and
riscv64 and verifies architecture-specific SHA256 and ET_EXEC/no
PT_INTERP. Both pre-existing fetch scripts delegate to
`scripts/fetch-busybox-fixture.sh`. A source code version match alone
is NOT proof of effective CONFIG values or libc behavior: the package is
the same Debian **busybox-static** recipe, and further effective applet
and Kconfig verification is a distinct gate. Alpine musl dynamic BusyBox
remains a SECOND, explicitly named fixture rather than being mixed into
Debian static x86/RV parity claims. AArch64 must be checksum-pinned
before it joins this package fixture, not silently choose some binary.

## Canonical BusyBox input fixture: test commands cannot drift by ISA
The shared tab-separated `scripts/busybox-parity-cases.tsv` contains the
EXACT 17 applet and shell workloads, expected output and process/pipe/exec
evidence. Architecture CI scripts must consume this file, not independently
write what they think is the same BusyBox test. The new common
`scripts/stage-busybox-rootfs.sh` stages identical CPIO file contents,
permissions, symlinks and timestamps for any native ELF. The architecture
determines ELF machine only; programs, input bytes and observable Linux
semantics are the same. This is independent of the additional x86 Alpine
dynamic-mUSL suite, whose libc/path/ELF differ and must be reported as
a separate profile.

## x86/RV parity is one executable contract, not Alpine vs Debian
`scripts/probe-x86-busybox-parity.sh` runs the **same** pinned Debian
1.37.0-6+b9 static BusyBox recipe as RV64, with the **identical 17-case
manifest and identical CPIO rootfs bytes/layout**. Both test native ELF,
actual QEMU/UEFI boot, Linux stdout/exit and process/FS/pipe evidence.
The existing x86 Alpine dynamic/musl suite is separately labeled rather
than being compared as if it were the same fixture. Select one case with
`BUSYBOX_PARITY_CASE=ash-fork` on either ISA to avoid repeating 16
known-passing cases while diagnosing one compatibility class.

## Effective applets, not just version strings
The RV64 QEMU CI job installs qemu-user and executes the exact official
Debian busybox-static binaries **natively on amd64 and through
qemu-riscv64 on RV64**. It compares sorted `busybox --list` applet sets
as a hard gate and checks all canonical test applets exist. This detects
many ISA/distro build-option differences; **it does not establish equality
of every Kconfig CONFIG_* option**, which requires extracting/rebuilding
the fully resolved upstream config separately. Do not call this a
fully config-identical fixture without that additional verification.
