# M8.0 — NaOS-style kernel subsystem foundations

NaOS's early history (April–May 2025) established arch, mm, task, fs and init before experimenting with sophisticated interchangeable algorithms. NaSeed first reorganizes **existing code**.

## Actual changes

- x86 GDT/IDT/TSS, FP/SSE, syscall MSRs and exception report: `arch/x86_64/cpu.c`.
- Architecture entry assembly and kernel linker script: `arch/x86_64/`.
- x86 user syscall saved frame, FPU and FS TLS state moved into `struct nas_arch_context` with save/restore code in `arch/x86_64/context.c`.
- Physical frame allocator and HHDM moved to `mm/frame.c`; allocation strategy unchanged.
- COM1 console moved to `drivers/serial.c`; QEMU PC debug-exit moved to `platform/qemu_debug_exit.c`.
- Initialization and Limine handoff remain in `kernel/kernel.c`.
- Makefile now declares ARCH=x86_64, selects architecture entry/linker and refuses unsupported ports.

## Boundaries *not yet established*

- The x86 4-level PTE walker, virtual memory policy, ELF, syscall logic and cooperative scheduler are **still mixed in m1.c**. Register/TLB wrappers do not make that code portable.
- The Linux syscall-number table is still x86-specific; no other ISA implementation or runtime mode switching exists.
- Current shared kernel stack / cooperative syscall-boundary switching remains unchanged.

## Evidence required before merge

Run preflight compile/link, full M0–M7.5 QEMU BusyBox/Bash/Alpine regressions including FD/pipe/tmpfs/fork/exec/shebang. No compile or QEMU success is claimed in this commit.
