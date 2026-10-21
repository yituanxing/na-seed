# NaSeed architecture contract v1 — portable semantics, replaceable mechanisms

Status: **design contract / migration target**, not an implemented HAL. This document does not change kernel behavior. Baseline: NaSeed main after M7.5 (real BusyBox resource-lifecycle regression).

## 0. Purpose and non-goals

- Adding an ISA must primarily add `arch/<isa>/` implementations, build/boot configuration, and architecture-matched userspace tests; it must not require copying FS, process, ELF, or syscall semantics.
- **Common semantics are mandatory; sharing a concrete algorithm is optional.** A correct generic implementation is the first reference, not a permanent performance ceiling.
- An architecture may replace an **entire implementation of a well-defined service**, rather than being forced to wrap every individual machine instruction. Selection must be explicit and testable.
- No runtime hardware-architecture plugin system: one ISA per kernel build. V1 implementation selection is at build time; paging mode is established during boot / address-space creation under explicit constraints.
- No premature Linux-sized framework: no new scheduler policies, 5-level page tables, SMP shootdowns, network stack or dynamic policy hot swap until a workload or new port justifies them.
- Do not turn unsupported ABI/operations into successful no-ops. `-ENOSYS` for missing syscalls, appropriate `-EINVAL`/`-EOPNOTSUPP`/`-ENOMEM` for supported calls with invalid/unsupported requests.

## 1. Dependency ownership

```text
                   Linux user ABI / syscall facade
                                 |
                        common subsystems
             FS/file | task/sched | MM/VM | ELF | boot
                |           |         |            |
                +-----------+---------+------------+
                                 |
                      published arch contracts
                                 |
              arch/x86_64/          arch/riscv64/ ...
                CPU/trap/context/MMU/ABI primitive providers

      platform/boot/driver is orthogonal to CPU ISA:
      Limine and device discovery != CPU architecture;
      PC COM1 / PL011 != CPU architecture.
```

Rules:
1. Common code may include only **public arch contract headers**, not private `arch/x86_64/*` headers or ISA-specific inline asm. The active build supplies implementations.
2. CPU instructions/register state, trap/exception frames, page-table descriptors, TLB maintenance, privileged user entry and userspace register ABI belong to `arch/`.
3. Scheduler decisions, task lifecycle, VMA policy, page ownership, ELF program-header traversal, CPIO/tmpfs/pipe/file semantics belong to common subsystems.
4. Boot protocol and board devices are distinct axes; an x86-only PIO UART driver must not become the generic console interface.
5. No ABI structures that match only x86 (e.g. a 15-register syscall frame or fixed FXSAVE 512-byte buffer) in common `struct nas_task`. Keep an opaque or compile-time-selected `arch_context` subobject.
6. Do not mistake a **directory move** for an abstraction: a common implementation that manipulates CR3, FS base or x86 PTEs is still x86-specific.

## 2. Three categories of reuse

- **Immutable semantic contract**: `vm_map` maps a permitted range or reports a defined failure; `task_wake` makes a blocked task runnable once; `read` obeys Linux FD behavior.
- **Optional generic reference algorithm**: 4 KiB page-table walker, FIFO/RR scheduler selection, simple byte copier; correct and portable *where an ISA can implement the required primitives*.
- **Replaceable specialized provider**: RISC-V tuned page-table walker with its own batching/large-leaf strategy, x86 tailored usercopy, alternative scheduler policy. Can replace generic mechanism behind the same contract without altering common call sites.

V1 selects **one provider per interface** in the build. Avoid weak symbols and scattered `#ifdef __riscv` branches in `mm/` or `task/`. Prefer explicit source selection that rejects zero or multiple providers. An arch-specific implementation **may call back into reusable common helpers**, but never has to use them.

### Selection model (conceptual; not yet Makefile code)

```text
ARCH=x86_64         MMU_IMPL=generic    SCHED_IMPL=rr
ARCH=riscv64        MMU_IMPL=generic    SCHED_IMPL=rr
ARCH=riscv64        MMU_IMPL=arch       SCHED_IMPL=rr
ARCH=riscv64        MMU_IMPL=arch       SCHED_IMPL=other_policy
```

`ARCH` selects ABI / CPU backend. `MMU_IMPL` selects a page-table algorithm provider. `SCHED_IMPL` independently selects scheduling policy (not necessarily ISA-specific). A scheduler optimized for RISC-V may be provided, but should not be coupled to the fact the target is RISC-V. A hardware paging **mode** (`Sv39/Sv48/Sv57`) is neither an ISA choice nor an algorithm provider choice.

## 3. MMU: multiple RISC-V paging modes with unchanged semantics

RV64 defines `Sv39`, `Sv48`, `Sv57` where supported. Hardware need not implement every mode. They imply different canonical virtual-address ranges and numbers of levels; they also permit different huge leaves. NaOS chooses RISC-V page-table levels from the boot paging mode in `kernel/src/arch/riscv64/mm/arch.c`. A page-table *format/width* is not an `mmap` promise.

Keep four concepts separate:
1. **Boot/CPU capabilities**: modes the platform actually supports, supported page sizes, effective ASID width, physical-address limits. Probe / learn from boot handoff; do not assume every RV64 CPU accepts Sv57.
2. **Chosen address-space mode**: an established `nas_mmu_layout` (e.g. Sv39) with effective `user_lo/user_hi`, alignment, legal mapping sizes. It is immutable for the lifetime of a live address space in v1. The bootloader's established kernel mappings must remain valid; changing `satp.MODE` on a live system is *not* a transparent optimization.
3. **Common VM policy**: VMA ownership, `mmap`/unmap/protect semantics, copy and fault decisions. Never derive a constant `USER_LIMIT` from the C target pointer width.
4. **Mapping provider**: how descriptors are traversed, leaves chosen, PTE bits/ordering encoded, batched TLB maintenance, ASID reuse. It may be a reference generic walker or a fully specialized per-arch walker.

### Contract example (illustrative C, not yet stable header)

```c
struct nas_mmu_layout {
    uintptr_t user_lo, user_hi; /* user VA bounds, architecture/mode chosen */
    uint64_t supported_leaf_sizes; /* capability representation TBD */
    unsigned page_shift;
};

struct nas_address_space; /* opaque to VM, owns active hardware root/mode */

int nas_pt_map_range(struct nas_address_space *as,
                     uintptr_t va, uint64_t pa, size_t len,
                     unsigned prot, unsigned attributes);
int nas_pt_unmap_range(struct nas_address_space *as,
                       uintptr_t va, size_t len);
int nas_pt_protect_range(struct nas_address_space *as,
                         uintptr_t va, size_t len, unsigned prot);
int nas_pt_query(struct nas_address_space *as, uintptr_t va,
                 uint64_t *pa, unsigned *prot);
```

Contractual constraints before adding fast providers:
- Same map/protect/unmap visible effect for every supported mode. Invalid/out-of-range/unaligned requests must fail explicitly; no silent truncation, wrapping or mode-specific fake success.
- `map_range` failure leaves no half-installed user mapping or leaked page-table frames; define rollback/cleanup and allocation ownership. A mode must not be chosen silently after page tables are live.
- Permissions are expressed as **logical** read/write/execute/user, not raw PTE bit positions; mode-dependent A/D-bit behavior must be handled correctly. User-visible `mprotect` must be enforced.
- Unmap/free ordering prevents use-after-free by stale TLB entries; a future SMP implementation must extend the synchronization contract for remote CPUs. Optimization may batch invalidations, not omit required invalidation.
- Page size selection is internal. A 4 KiB implementation is the v1 baseline; an arch provider may use huge leaves where alignment, permissions, mappings and splits permit. `mprotect` and unmap of a subrange must still work (split a huge mapping or reject an *unsupported* operation explicitly).
- `user_hi` and placement policy derive from the selected mode, not a hardcoded x86-64 `0x0000800000000000`. Kernel/user range and root-mapping compatibility must be checked at initialization.
- An AArch64 configuration with a different translation granule is not promised by v1: if the common VM requires a 4 KiB base page, the port must explicitly advertise and satisfy it or fail startup. Do not claim a generic backend supports a mode its primitives cannot express.

**Mode choice in v1:** boot-time negotiation/configuration, one selected mode for the kernel's compatible address-space layout. `auto` chooses a validated supported mode; explicit `sv39/sv48/sv57` must reject an unsupported request. Switching a live process from Sv39 to Sv48 is **out of scope**; later mode transitions require their own preservation, atomicity and TLB tests.

## 4. Scheduler: same invariants, *different legitimate policies*

The generic scheduler **core** owns task state, transitions `READY/RUNNING/BLOCKED/ZOMBIE`, wait/wake/reap, legal CPU switch points, and queue membership consistency.

The selected **policy** owns runnable ordering and selection (initial RR; future priority, fair/EEVDF-like or ISA/workload tuned policy). Swapping an algorithm may intentionally change latency, fairness and scheduling order — **these are policy effects, not kernel semantic violations**. No universal "same scheduling order" guarantee.

Conceptual hook boundary:
```c
struct nas_sched_policy {
    void (*enqueue)(struct nas_task *);
    void (*dequeue)(struct nas_task *);
    struct nas_task *(*pick_next)(void);
    void (*on_tick)(uint64_t now_ns);  /* optional */
};
```

Non-negotiable core invariants: no blocked/zombie task selected; no task duplicated/lost in runnable queues; a runnable task is either current or eligible for selection; sleep/wake and fork/exit/wait resource ownership remain correct. Only policies that explicitly promise fairness require starvation bounds. Policy-specific state stays outside arch register-context state.

CPU context save/restore and kernel/user switch are **arch mechanism**, never scheduler algorithm. Timer source / IPI are platform/arch services. Do not migrate the current single shared syscall stack, cooperative-only switching assumption into a purported portable task ABI: the initial extracted backend can keep that restriction explicitly until per-task kernel stacks/preemption are implemented.

Policy replacement v1 happens at build/boot; **runtime hot switch is not supported** unless runnable queues and policy state can be migrated safely.

## 5. Other future overrides (only when warranted)

| Shared contract | Generic baseline | Possible specialized mechanism | Needed invariant |
| --- | --- | --- | --- |
| Syscall | common dispatch + fs/task/mm handlers | arch entry, number table, register-ABI adapters | Linux ABI arguments, errors, special frame-aware syscalls |
| Context | task state and scheduling decision | save/restore regs, TLS, FP/SIMD, user entry | no stale task state, correct fork/exec return |
| ELF | parse PT_LOAD/INTERP and VM requests | e_machine, initial user regs, TLS, stack ABI | correct arch ELF executes; no x86 assumptions |
| User copy | checked bytewise reference semantics | arch fault-safe copy/CPU optimized paths | EFAULT, no kernel crash, no partial-success lies |
| File objects | FD lifetime, offsets, read/write/poll | backend ops for CPIO/tmpfs/pipe/device | dup/fork/close semantics; no magic FD kinds in syscall layer |
| Logging | common console writes | UART PIO/MMIO, other device drivers | backend does not dictate CPU ISA |
| Atomic ops | portable memory-order contract | ISA atomics/barriers | acquire/release/seq-cst meanings preserved |

Note: generic algorithms may be expensive, but **correctness baseline is more valuable than maximum reuse**. Introduce specialization because of actual limits or measured costs, not because an architecture has a new directory.

## 6. Provider boundary rules (must be enforceable)

1. Public operation names/types/errors belong to shared headers, **not** to `arch/<isa>` private code.
2. Exactly one provider selected for each interchangeable service at link time; no weak override, silent fallback or duplicate registration.
3. `ARCH`, `MMU_IMPL`, `SCHED_IMPL` and MMU *mode* are separate documented dimensions. Unsupported combinations fail loudly before userspace.
4. Generic/reference and specialized implementation use the **same behavioral tests**; specialized implementations add arch/mode-specific tests.
5. No invented "universal capability" that silently downgrades semantics; return a defined error or refuse unsupported startup profiles.
6. Performance optimizations can change algorithm, batching and cache strategy, but not exposed correctness and lifetime contracts.
7. Document capabilities and restrictions from **what has actually been tested**, not just what is representable in the interface.

## 7. Concrete NaSeed extraction order

**V1-A: arch boundary on existing x86; no behavior change**
- Move x86 GDT/IDT/TSS, CRx/MSR/TLB/FPU, syscall machine entry, user-entry and saved CPU frame behind `arch/x86_64/` with narrow contracts.
- Move x86-specific user mapping/PTE code out of common code, preserving the existing correct algorithm as the first x86 provider. `nas_task` references an arch-specific context, not `resume_regs[15]` / `fpu_state[512]` in common.
- Separate fixed x86 build/linker and COM1 device handling from common boot entry. Do not conflate boot protocol with ISA.
- **Acceptance:** existing M0–M7.5 BusyBox/Bash/musl+CPIO/tmpfs/pipe/fork/exec/FD and QEMU regressions unchanged. Static gate: no x86 asm in common `task/mm/fs/syscall/ELF` paths.

**V1-B: common syscall/VM/task boundaries; no speculative features**
- Central syscall dispatch, with actual Linux ABI adapters per arch. FS/MM/task system-call functions live next to the owning subsystem.
- Expose VM-owned address-space handle and selected layout/capabilities; VM calls one selected MMU provider.
- Scheduler core separates from policy selection and arch save/restore. Keep existing cooperative behavior as **explicit temporary capability limit**.
- **Acceptance:** same x86 executable bytes and workloads, same externally observable syscall semantics; invalid inputs and resource exhaustion covered.

**V1-C: test replaceability before porting**
- Provide a simple reference/generic page walker where valid, plus ability for an ISA to replace the full page-table provider at build time. Prove both with a common suite if both exist.
- Provide baseline RR scheduler policy and a second *test* policy only when core/policy split is mature; prove block/wake/reap invariants independent of pick order.
- **Acceptance:** changing provider changes only the selected implementation objects / configuration, **not** `mm`, `task`, `fs`, or syscall code. CI guards against accidental build coupling.

**V1-D: second-ISA proof (RISC-V or AArch64)**
- ISA boot + console; independent page table mode and user VA limit; exception + syscall; minimal correct user ELF; fork/exec/task resources; architecture-matched real BusyBox.
- RISC-V-specific testing when selected: supported Sv39/Sv48/Sv57 profiles; invalid mode rejection; canonical VA boundaries, huge page split/unmap if implemented, ASID/TLB behavior.
- No statement of "multi-arch support" before real QEMU userspace and ABI-level tests pass.

## 8. Exclusions until demanded

Dynamic switching of MMU paging mode for live processes; runtime scheduler-policy migration; SMP/IPI global TLB shootdowns; transparent large-page promotion/demotion; arbitrary AArch64 translation granules; dynamically loaded architecture backend; kernel-wide giant `ArchOps` virtual table.

## References / current code debt

- NaSeed `kernel/m1.c`: mixed syscall, x86 page table, TLS, GDT/IDT, ELF, task/scheduler; `kernel/task.h`: fixed x86 saved frame and FPU state; `kernel/kernel.c`: PC COM1 mixed with boot and physical frame bitmap; `Makefile`: x86-only compiler/linker configuration.
- NaOS `kernel/src/arch/riscv64/mm/arch.c`: adjusts page-table level count based on selected boot paging mode.
- NaOS `kernel/src/arch/x86_64/task/arch_context.h` vs `arch/aarch64/task/arch_context.h`: arch-specific contexts behind shared contract.
- RISC-V privileged specification, supervisor address translation `satp` and Sv39/Sv48/Sv57: https://docs.riscv.org/reference/isa/priv/supervisor.html
- Linux scheduler design (scheduling classes): https://docs.kernel.org/scheduler/sched-design-CFS.html ; sched_ext: https://docs.kernel.org/scheduler/sched-ext.html
