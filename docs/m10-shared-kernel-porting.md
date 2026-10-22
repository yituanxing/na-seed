# Shared kernel, architecture-owned mechanisms

The porting goal is one reusable NaSeed kernel, not separate M0 user kernels.
NaOS (kadds/NaOS) was inspected for its single kernel initialization,
arch/task split, shared ELF load_common and scheduler-policy boundary.
Its x86-specific paging.h is not a multiarch page-table contract.

## Next native-workload gate: explicit anonymous VM rights (2026-10-21)

The first 17 static BusyBox cases expose no guarantee about anonymous
PROT_READ / PROT_EXEC mappings. Previously `nas_vm_anon_mmap` passed a
single `writable` boolean to `nas_mmu_map_page`; a nonwritable page
could be executable, and on x86 writable pages could remain executable
until a later `mprotect`. This is an observable Linux ABI mistake, not a
reason to build a full VMA subsystem before its requirements are known.

- Anonymous mmap now requests **exact permissions at initial PTE mapping**,
  via `nas_mmu_map_prot_page`; x86 explicitly sets NX, while RV/ARM use
  the same common space lifecycle and translate R/RW/RX to native PTEs.
- W+X is explicitly refused with `-EOPNOTSUPP` (including mprotect);
  `PROT_NONE` is explicitly `-ENOSYS` until a VMA tracks inaccessible
  but still-owned pages. No fake PROT_NONE -> readable mapping.
- Host test covers supported/unsupported policies; native RV/ARM physical
  PTE regression checks R versus RX and refuses W+X without changing rights.
  Existing real ELF/Bash/BusyBox QEMU regression remains mandatory.
- This is deliberately narrower than NaOS's `vm::info_t`/VMA and
  COW/lazy pager features: future dynamic ELF, file mappings and page
  faults should inform that larger design. The legacy ELF eager PT_LOAD
  mapping contract is unchanged in this pass, and still needs a separate
  page-granular permission review before claiming universal W^X.


## Native dynamic Alpine exploration — diagnostics, not acceptance

The already-green static Debian 17-case matrix remains the mandatory gate.
Use `scripts/probe-alpine-native-dynamic.sh arm64|riscv64` to boot the SAME
full kernel with original Alpine v3.23 ET_DYN BusyBox 1.37.0-r30 and the
original native musl interpreter 1.2.5-r23 from a real CPIO rootfs.
The first probe requests `echo hello`; archive layout and ELF machine/ABI
are checked, and serial logs capture the first real failure, without
inventing success, modifying binaries or adding a per-ISA runtime.

**Exploratory-only security gate:** the four initial package SHA256 values
are deliberately marked `-` in `scripts/alpine-native-fixtures.txt`; the
diagnostic prints the actual downloaded digest. They must be pinned in a
follow-up before interpreting any successful run as verified native Alpine
compatibility. CI marks exploratory steps `continue-on-error` so dynamic
failures cannot revoke the accepted Debian static baseline. This
investigation should inform VM/VMA/ELF loader design instead of prompting
immediate broad refactoring.

## SHA256-pinned Alpine dynamic userspace: 7-case cross-ISA expansion

Exploratory native Alpine dynamic `echo hello` completed on **both** real
ISA QEMU boots: arm64 #39619274956 (after fixing native build-dir name
in `afde101c`) and riscv64 #39618675840. These are true original
Alpine BusyBox 1.37.0-r30 ET_DYN + musl 1.2.5-r23, loaded from CPIO
through the common kernel's ELF/PT_INTERP path.

The four archive SHA256 digests observed in actual official-package
downloads are now **pinned** in `scripts/alpine-native-fixtures.txt`;
unrecorded/unverified hashes abort before boot. Both native CI workflows
execute seven fixed dynamic-workload cases: echo, cat, ls, shell builtins,
fork/exec/wait, pipeline and tmpfs file lifecycle. Every native QEMU boot
asserts actual dynamic interpreter entry, actual guest stdout, exit 0,
and architecture-specific exit telemetry.

**First native dynamic 7/7 evidence:** ARM64 [#39620119229](https://github.com/yituanxing/na-seed/actions/runs/39620119229)
and RV64 [#39620119460](https://github.com/yituanxing/na-seed/actions/runs/39620119460)
both printed `[ALPINE NATIVE MATRIX PASS] 7/7` in their completed
job logs. The test steps were previously exploratory with
`continue-on-error`; follow-up removes that exemption and makes BOTH
native dynamic 7-case matrices mandatory along with Debian static 17/17.
Do not infer general ELF or Linux compatibility beyond these scenarios.

## Two software routes: unchanged Linux binaries + controlled-source BusyBox

**Do not confuse controlled recompilation with a NaSeed-native ABI.**
The official Debian static 17-case and Alpine musl dynamic 7-case binaries
remain immutable Linux compatibility acceptance. A second controlled-source
route builds upstream BusyBox 1.37.0 (fixed SHA256
`3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4`)
with one explicit `scripts/source-busybox.config` across **amd64, riscv64
and arm64**. This route uses existing Linux ABI and target static glibc;
it does **not** add an mlibc fork, a new syscall ABI or a second kernel core.

- `sh scripts/build-source-busybox.sh <arch>` verifies upstream source
  integrity, resolves Kconfig with fail-closed feature checks, builds
  native static ELF and checks `--list` + `echo` against the host or
  real QEMU Linux-user oracle. Outputs and verified config are saved in
  `build/source-busybox/<arch>/`.
- `sh scripts/probe-source-busybox.sh <arch>` uses the **existing actual
  NaSeed kernel QEMU boot harness**, shared Task/ELF/FS/VM and CPIO rootfs,
  running six source-profile scenarios including shell fork/exec, pipelines,
  tmpfs and four sequential shell pipeline iterations in a single boot.
  This intentionally tests state reuse beyond single applet boot.
- The three canonical parity runners now support an explicit alternate
  immutable `BUSYBOX_PARITY_BINARY`, `BUSYBOX_PARITY_MANIFEST`, and
  `BUSYBOX_PARITY_EXPECTED_COUNT` without changing their default Debian
  17/17 behavior or overwriting the Debian fixture. No duplicated
  architecture-specific ELF/userland execution paths.
- Development workflow: finish an OS-capability-sized batch and inexpensive
  `scripts/check-source-busybox.sh` first, then one aggregated CI pass
  with three parallel native source profiles. Treat a new profile as not
  accepted until actual QEMU logs prove it. Controlled source is an
  ability-driver; immutable distro binaries remain the compatibility oracle.

## Current verified integration status — 2026-10-21

The sections below retain the chronological engineering trail, including
older *not yet integrated* statements from intermediate milestones. Those
historical blockers have now been retired as follows:

- Real x86-64, RV64, and AArch64 QEMU full kernels link and execute the
  same Task, process/scheduler, ELF, syscall, and FS C modules. Each runs
  the original pinned Debian 13 static BusyBox 1.37.0 through the same
  17 canonical cases in `scripts/busybox-parity-cases.tsv`.
- The completed ARM64 native matrix is CI #39609991928 (17/17), and the
  three-ISA userspace regression is #39609991837 (all jobs SUCCESS).
- RV64 and AArch64 share `mm/user_space.c` (physical frame lifetime,
  fork clone, mprotect, reclaim), with ISA-native user page tables and
  context/trap providers. The original x86 MMU provider remains separate.
- All full non-x86 builds now read `scripts/full-kernel-core.sources`:
  shared-core membership has one source of truth instead of independent
  duplicated shell lists. Hardware provider selections remain ISA-specific.
- AArch64 Linux O_* numeric flags normalize in `arch/linux_fcntl_abi.h`;
  the portable ARM translation receives its own host-executed ABI tests.
- Verbose ARM-only SVC tracing can be re-enabled at build time by defining
  `NASEED_TRACE_ARCH_SYSCALL`; normal core VM/init code does not contain
  ARM diagnostic policy.

**Limits:** This proves the fixed BusyBox workload, not unrestricted Linux
application compatibility. Dynamic ARM64 Alpine, SMP/preemptive scheduling,
threads/futexes, and unifying x86 with the RV/ARM user-space lifecycle
remain separate objectives. The root Makefile still defaults non-x86
`port-kernel` to the M0 smoke; use the explicit full build scripts for the
integrated kernels until the build entrypoints are consolidated.

## Historical porting log: whole-codebase audit and earlier blockers

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

## Native clone syscall argument order is architecture-owned
Official Debian x86 BusyBox 1.37.0 produced clone(56) on the same
three real shell cases which RV64 passes using clone(220). Linux native
x86 syscall clone's FOURTH argument is child_tid, but Linux generic64
RV64/ARM64's FIFTH argument is child_tid (fourth is TLS). The ABI
translation `nas_linux_clone_child_tid` selects the right user pointer
before invoking ONE shared process fork/TID implementation. Only exact
CLONE_CHILD_SETTID|CLONE_CHILD_CLEARTID|SIGCHLD with a null alternate
child stack is accepted, rejecting CLONE_VM/VFORK/THREAD. Both x86 and
asm-generic64 host unit tests verify register layout and rejection.
Real x86 BusyBox runtime CI still needs confirmation: do not report
17/17 until it has completed.

## AArch64 full shared kernel hardware backend (not another OS)
The ARM64 native build now links the SAME generic C translation units as
x86/RV64, with no NASEED_PORT_M0 and NO duplicate Task, process,
scheduler, ELF, execve, FS, pipe, or syscall code. Architecture-owned
EL0 context/CPU trap handles genuine SVC from native ELF; caller saves
and restores all 31 GPRs, PC/SP/PSTATE, TPIDR_EL0 and NEON q0..q31,
FPCR and FPSR. `nas_arch_prepare_user_cpu` first transfers Limine's
kernel SP_EL0 stack address into SP_EL1 and selects SPSEL=1 before
reusing SP_EL0 for userspace (the prior #44 EL0 smoke stalled after
SPSEL=0). Real TTBR0 39-bit 4KiB and user MAIR index 7 are managed
alongside Limine's preserved high TTBR1 kernel mapping.
The first true AArch64 full-kernel QEMU acceptance is a native ELF write
and exit executed through the SAME generic dispatcher. This is NOT
yet a claim that original BusyBox works: its ABI runtime needs real
validation and the 17-case parity manifest, not a separate new C library.

## Root cause of AArch64 bootstrap stall: changing banked stack under C
The full AArch64 kernel reliably reached common Task boot, then printed
`[ARM64 CPU] preparing SPSel` and hung before the next print. This
identified the actual EL1 PSTATE.SP bank switch, previously performed
inside a live C function. New ARM full-kernel **ELF entry symbol**
`nas_arm64_bootstrap` transfers Limine's SP_EL0 boot stack to SP_EL1,
selects SPSel=1, and tail branches to the EXACT existing kmain BEFORE
the first C call frame. `nas_arch_prepare_user_cpu` now asserts that
contract and never tries to switch SP banks mid-C-function. The M0-only
kernel continues using its own proven boot entry and does not link this
ARM full symbol. The isolated QEMU full test is now bounded to 25 sec
per bringup attempt, but still requires full Linux user write+exit.

## ARM64 native Debian BusyBox parity fixture — no platform test shortcuts
The exact official Debian `busybox-static 1.37.0-6+b9` for arm64 is
pinned with SHA256 `c833be48abfa16bc19c4966ec93e289ff1ce5d2f1476cad3a57bd105378cd15c` (Debian package archive), same upstream
revision and recipe as amd64/RV64. `scripts/probe-aarch64-busybox-matrix.sh`
is wired to the **existing** `scripts/busybox-parity-cases.tsv` (17
identical workloads) and common CPIO staging. Separate, fast ARM64
full-kernel CI initially gates just `BUSYBOX_PARITY_CASE=echo` to expose
ELF/ABI issues with the untouched native binary before enabling all 17.
Never report ARM64 parity as passed until the entire suite is green.

## First genuine arm64 BusyBox blocked AFTER printing hello
Official pinned native AArch64 Debian BusyBox runs via real full shared
kernel, performs Linux syscalls and prints literal `hello`, but then
raises hardware EL0 ESR=0x9200004f (data permission fault on WRITE),
PC=0x404794 and FAR=0x5dfa10 during exit/runtime cleanup. This is NOT
a missing ELF loader, wrong printf output or fabricated success. Added
PT_LOAD/RELRO segment evidence and actual ARM user PTE writable and
mprotect range diagnostics. Must fix real ARM ABI/VM semantics before
claiming even BusyBox echo pass, without modifying the original binary.
