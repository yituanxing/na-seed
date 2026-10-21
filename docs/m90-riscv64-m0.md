# M9.0: architecture implementation drives the audit

This is a **real riscv64 Limine M0 ELF**, not a kernel with pretend Linux support.
`make ARCH=riscv64 port-kernel` builds the original Limine handoff,
physical frame allocation and free smoke, with RISC-V SBI console output.
RISC-V supervisor SATP is checked against the Limine paging-mode request
(Sv39/Sv48/Sv57), avoiding any hard-coded Sv48 assumption.
x86-64 full userspace remains entirely as before.

The CI gate tests cross-compilation + ELF machine identity, NOT QEMU
runtime on RISC-V; no RISC-V user-mode trap, syscall, or MMU backend is
claimed. The next verifiable goal is real QEMU virt/UEFI/OpenSBI output.

Audit comparing NaOS's four backends:
- `syscall/dispatch.c`, `fs/syscall.c`, `task/sched.c` use x86-64
  `__NR_*` numbers and cannot be reused as-is for RISC-V or ARM.
- `task/elf.c` and `task/user_stack.c` hardcode the x86-64 dynamic
  interpreter, ELF machine / user stack layout.
- `fs/syscall.c` must separate x86-64 stat and directory ABI encodings.
- `drivers/serial.c` is PC COM1 port I/O. RISC-V uses SBI for now.
- aarch64 and loongarch64 will join as the next actual boot/ABI ports,
  rather than being listed as supported without code or test evidence.
