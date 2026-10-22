# M24 — standard signal pending state, disposition and Linux ABI

M23 verified terminal-generated default SIGINT/SIGTSTP. M24 adds Linux
kill(2) default process termination for SIGINT, SIGQUIT, SIGPIPE and
SIGTERM, plus SIGKILL. Each task holds a pending standard signal bitset.
Masked standard signals are queued rather than immediately terminating.
Linux rt_sigpending(127 x86 / 136 generic64) reports exactly the pending
signals that are blocked. rt_sigprocmask unblocking a pending default
terminating signal must terminate the current task immediately at the
existing syscall boundary: it may not resume past the unmask call.

Ignored signals are discarded, and ignored dispositions survive execve;
caught handlers are reset on exec. Fork inherits mask/disposition but not
pending state. New sigaction buffer copying is transactional: validate
new action input before copying out the previous state or committing.

**Explicit missing feature**: arbitrary user sigaction handlers, user
rt_sigframe/rt_sigreturn, signal trampolines and pending caught-signal
delivery are NOT yet implemented. The native group signal path fails
with EOPNOTSUPP rather than claiming a fabricated user handler, and a
mask transition cannot unmask pending caught signals without that support.

Acceptance: one unchanged Linux C standard executable exercises real
fork/pipe synchronization, SIGTERM masked delivery plus sigpending, exact
WIFSIGNALED on unblock, SIG_IGN, self SIGTERM and ignored disposition
across execve. It runs on native Linux/QEMU-user, then each full NaSeed
x86, RV64 and ARM64 kernel in the same immutable original source BusyBox
harness. All prior 13 controlled program profiles remain and grow to
14, while all original Debian static and Alpine dynamic binaries are
unchanged. M18-23 interactive terminal regressions remain mandatory.
