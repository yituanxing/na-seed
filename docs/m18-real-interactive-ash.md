# M18 — run original BusyBox ash as an actual interactive POSIX shell

## User-visible requirement

M17 is capable of reading *real* serial bytes but did not boot a real shell
with owning terminal file descriptors. M18 starts a **single genuine native
Linux PID1** `tests/m18-ash-init.c`, calls existing Linux TIOCSCTTY, opens
the real `/dev/tty`, attaches it to stdio with dup2, then **execve**
replaces PID1 with the pinned unmodified upstream BusyBox 1.37.0 ash
in interactive mode (`/bin/busybox sh -i`). No fake shell command
interpreter, libc syscall, shell prompt injection or kernel program-name
special case.

## Rigorous test: x86 real QEMU COM1

QEMU starts only with a CPIO archive containing the actual GNU/Linux native
`m18-ash-init` and the same SHA256-pinned original upstream BusyBox ELF.
`scripts/m18-ash-serial.py` observes ash's actual PS1 prompt over the
physical COM1 UART, then sends genuine keyboard bytes and Enter through
`-serial stdio` after each prompt. It verifies built-in echo, genuine
process/pipe/wc, cwd, repeated command prompt and clean `exit`.
The harness sends one byte at a time; not an invisible preloaded input FIFO.

The same PID1 ELF source is compiled for x86-64, RV64 and AArch64 and its
Linux PTY oracle runs on native Linux/QEMU-user across all three ISAs.
Actual live serial input requires a physical receive backend: x86 COM1.
RV64 SBI and AArch64 semihosting remain output-only; no fabricated inputs.

M17's unmodified original Debian/Alpine binaries and complete 13-case
three-ISA source BusyBox matrix remain mandatory. This feature does not
weaken existing ABI coverage.

## Scope beyond M18

An interactive prompt and execution of commands are not full job control.
SIGINT/SIGTSTP TTY signal delivery, saved handler frames, SIGTTIN/SIGTTOU,
preemption, asynchronous RX wakeups and native receive drivers for RV/ARM
are explicitly deferred; don't claim working Ctrl-C/Ctrl-Z.

## M19 architecture parity (real bytes, no separate Shell)

RISC-V QEMU virt's OpenSBI legacy getchar (SBI EID 2) and AArch64
QEMU virt's actual PL011 at 0x09000000 now provide input to the SAME
TTY and upstream BusyBox ash executable as x86. The PL011 page receives
an explicit MAIR Device-nGnRnE kernel TTBR1 PTE; current Limine HHDM is
restrictive and cannot be assumed to cover arbitrary MMIO.

`probe-m18-port-interactive.sh` stages the source-profile CPIO into a
real per-ISA UEFI disk. `m18-ash-serial.py` consumes actual UART
data, responds only to genuine ash prompts, sends five live commands,
then checks guest Linux syscall exit and ISA-specific kernel exit
marker. ARM keeps semihosting for early boot before its PL011 mapping.
No kernel interpreter or per-ISA Linux syscall shims are introduced.

**Hardware qualification:** These are QEMU virt platform backends:
OpenSBI legacy console requires platform firmware support; a future
RV64 firmware without that deprecated extension needs DBCN read.
The PL011 physical address is the QEMU virt board constant, not a
discoverable universal ARM address. Other boards require DT/ACPI
discovery and the established MMIO mapping service. No assertion of
arbitrary ARM/RV board serial compatibility is made.
