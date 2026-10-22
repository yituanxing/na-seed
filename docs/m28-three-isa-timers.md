# M28 second provider batch: RV64 SBI TIME and AArch64 GICv2 virtual timer

This PR stacks on M28 x86 PR #68 and changes only architecture-specific
providers and their original BusyBox QEMU acceptance, not Task ABI, FS,
syscall semantics or the process scheduler. PR #68 must merge first.

- RV64: QEMU virt 10MHz `time` CSR, SBI TIME extension probe and
  `set_timer`, supervisor STIE after stvec/sscratch initialization.
  User STIP (scause=interrupt|5) does **not** advance sepc. All 32 GPRs,
  user SP/PC/status/TLS and hard-float FCSR are preserved, with the same
  cooperative blocked syscall replay for tasks previously parked.
- ARM64: actual ARMv8-A Generic Virtual Timer CNTV_CVAL_EL0 with period
  read from CNTFRQ_EL0, QEMU virt GICv2 PPI27 via real distributor/CPU
  interface MMIO. The existing PL011 Device-nGnRnE page-walk was
  factored into an arch-owned `nas_arm_map_io_page`, shared with GIC.
  Exact same GPR + 280-byte trap frame layout is saved by a single
  assembly macro for EL0 SVC and EL0 IRQ. EL0 PSTATE.I unmasked only
  after timer + GIC initialization.
- No timer IRQ is converted into a fabricated syscall; the interrupted
  return PC is left unmodified. IRQ handler is never a general kernel
  preemption path. Unknown vector, GIC INTID, SBI failure or kernel-
  mode delivery fails closed; there is no fallback to pretending success.

## Exact Linux oracle, not a NaSeed-written fake workload

`tests/m28-preempt.sh` executes with **unmodified pinned upstream BusyBox
1.37.0 ash** on native Linux, x86 QEMU, and both new QEMU architectures.
Producer writes a pipe readiness word, then runs a pure userspace
`while :; do :; done` loop. Another runnable task must print
`cpu-hog-did-not-starve-ash`. On the child-first cooperative scheduler,
that cannot occur: only a hardware timer IRQ can preempt the hog.

RV64 and ARM64 one-case manifests are independent from 16 existing
source BusyBox cases, native Debian/Alpine fixtures, and interactive
TTY/signal jobs. A pass requires BOTH user stdout and real
`[M28] timer-preempt from=` trace (bounded by 45s per QEMU test).
The program never returns; a successful monitor explicitly stops QEMU.

Fast check: `sh scripts/check-m28-port-timers.sh`.
CI is the only source of truth for real interrupt delivery; compilation
alone is not runtime evidence. No multiprocessor, preempted kernel C
continuations, CFS, or generic device interrupt framework is claimed.
