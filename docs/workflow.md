# Real-software-first development

The long-term core of NaSeed is **real device driver support**, not just a Bash demo.

- M0: Limine + memory map/HHDM, serial UART, conservative bitmap allocator. QEMU smoke test is mandatory.
- M1: CPU exceptions, user mappings, static ELF and userspace ring-3 entry; Linux x86-64 syscall gate with write/exit, trace and -ENOSYS for missing calls.
- M2: Fixed static musl + GNU Bash loaded from initramfs, actual bash -c echo. Repair related semantic failures in batches.
- M3: Serial/keyboard-backed TTY, interactive Bash; then fork, exec, wait, pipes, signals and real external commands.
- Drivers: grow the driver model from real requirements: interrupt-driven serial and keyboard, PCI enumeration, block I/O, AHCI/NVMe, etc. Test both QEMU and available physical hardware.

Loop: **run real binary -> collect syscall/exception logs -> group failures -> fix semantics -> rerun -> keep regression test**.

Do not fake successes for absent syscalls. Do not reclaim Limine-owned bootloader memory while using its page tables or responses. Do not claim a stage until CI/runtime confirms it.

NaOS, Kerla and Maestro are **reference implementations**, not sources to copy wholesale.
