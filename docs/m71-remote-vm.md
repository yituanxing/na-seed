# M71 kernel-internal selected VM root read (not ptrace)

The production common `nas_vm_read_task_bytes` traverses *another*
Task's VM owner and real native user page tables, without switching
CR3 / satp / TTBR0. Distinct CPU backends export exactly one selected
root-to-physical translation interface; the common 44th kernel TU owns
page-boundary copying. Missing pages return EFAULT with zero progress
or a partial byte count. Dead targets fail ESRCH.

**NO user-facing syscall, tracing relationship or authorization exists.**
This primitive MUST remain kernel-internal until tracer credentials,
dumpability, ptrace-stop, wait4 trace events and per-ISA register GETREGSET
exist. A read-only memory primitive is not proof of strace support.

Host check links the genuine production remote_copy.c with controlled
noncontiguous page tables and verifies selected-root ownership, CLONE_VM
shared owner, partial read, overflow and dead-target faults. Native CI
checks all three full kernels compile/link and retains every preexisting
original-program milestone without special-purpose syscall bypass.
