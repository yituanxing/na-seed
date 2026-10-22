# M30 — native user signal frames / rt_sigreturn, cross-ISA contract

## Starting point (verified in the actual NaSeed tree)
- M23–25 have real default/ignore signal dispositions, masks, pending
  standard signals, SIGKILL/STOP/CONT groups and TTY job control, but
  **deliberately reject** caught handlers rather than pretend they ran.
- x86 SYSRET was replaced with IRET for timer preemption; RV64 ECALL/IRQ
  share saved trap state and use SRET; AArch64 SVC/IRQ share EL0 trap state
  and use ERET. All three have per-task kernel entry stacks but only the
  syscall-boundary kernel scheduler (no resumable kernel C continuations).
- M29 adds monotonic task deadlines and nanosleep/poll; signal arrival
  must now *interrupt* a parked syscall with EINTR + correct remainder.

## Native Linux requirements — no generic homemade user signal ABI
1. Define a native, validated rt_sigframe for **each ISA**, matching the
   Linux user-visible ucontext/siginfo layout and preserving all user GPR
   state, original masks and FP/SIMD state. Create it in the task's own
   address space **only at user-return boundary** (syscall or IRQ), after
   the scheduler selects the actual target Task.
2. x86-64: preserve the 128-byte red zone and 16-byte call-stack ABI,
   use SA_RESTORER and a valid executable libc restorer pointer; RDI=signum,
   RSI=&siginfo, RDX=&ucontext only when appropriate; restore via IRET.
3. RV64: use 16-byte aligned frame, RA to a valid executable rt_sigreturn
   trampoline (kernel vDSO, not a guessed executable user stack), A0=signum,
   A1/A2 native siginfo/ucontext; restore native SEPC/SSTATUS/FP/TLS.
4. AArch64: 16-byte aligned native frame including FPSIMD context,
   X30=valid libc SA_RESTORER or mapped VDSO sigtramp; X0=signum,
   X1/X2 info/ucontext; restore ELR/SPSR/TPIDR and full NEON/FPU.
5. rt_sigreturn returns **to original user context**, never to kernel C.
   Validate untrusted user sigframe and reject broken/restorer-missing
   frames safely (signal default SIGSEGV or precise -EFAULT); do not
   allow restoring privileged CPU bits or another Task's saved state.
6. Standard signal coalescing, per-handler blocking, SA_NODEFER/RESETHAND,
   SIG_IGN, unmaskable SIGKILL/STOP and fork/exec inheritance must agree
   with Linux. A signal caught during nanosleep/poll/ppoll interrupts it with
   EINTR rather than making it wait until the absolute deadline. ppoll's
   temporary signal mask must be installed atomically and restored on exit.
7. Hook delivery at *all* user return gates (including timer IRQ) so a
   spinning user process receiving a signal is not indefinitely starved.
   No user memory dereference while the target's page table is not live.

## Acceptance, not invented success
- `tests/m30-signal-handlers.c` is a single static source built with
  native amd64, RV64, ARM64 Linux libc and executed on real Linux or
  qemu-user first. It checks SIGUSR1 SA_SIGINFO, SIGUSR2 ordinary handler,
  signal mask observed *inside* handler, standard coalescing, pending
  unmask delivery, and a deterministic pipe-handshaked pending signal
  interrupting ppoll while it atomically unblocks that signal.
- Next integrate the same ELF in the existing original BusyBox full-kernel
  matrix with identical outputs; keep M23–29 unchanged regressions.
- Any ISA without a complete native signal frame/return implementation
  must keep returning explicit EOPNOTSUPP, not claim parity.

## Reference implementation evidence
Linux: arch/x86/kernel/signal.c, arch/riscv/kernel/signal.c,
arch/arm64/kernel/signal.c and arch/arm64/kernel/vdso/sigreturn.S.
The three Linux ABI frames and restorer arrangements ARE different.
NaOS repository did not contain a corresponding rt_sigreturn implementation
in its indexed source, so use actual Linux ABI rather than inventing one.
