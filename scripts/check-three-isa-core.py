#!/usr/bin/env python3
"""M50: verify the actual linked x86 and both full-kernel source sets.

The historical manifest guarded only RV64/AArch64. A common kernel TU
omitted from the x86 linker would otherwise pass shared cross-ISA audits.
"""
from pathlib import Path
import re
root=Path(__file__).resolve().parent.parent
srcs=[line.strip() for line in (root/"scripts/full-kernel-core.sources").read_text().splitlines()
      if line.strip() and not line.startswith("#")]
assert len(srcs)==len(set(srcs))==59, "common kernel TU manifest drift"
make=(root/"Makefile").read_text()
defs={}
for line in make.splitlines():
    m=re.match(r"^\$\(OUT\)/([a-z0-9_]+\.o):\s+(kernel/[\w/]+\.c)(?:\s|$)",line)
    if m:
        if m.group(2) in defs:
            raise SystemExit(f"duplicate x86 source object mapping: {m.group(2)}")
        defs[m.group(2)]=m.group(1)
linked=re.search(r"^\$\(KERNEL\):\s+([^\n]+)",make,re.M)
if not linked:
    raise SystemExit("x86 kernel has no object linkage dependency rule")
actual_link=re.search(r"^\s*ld\.lld \$\(LDFLAGS\)\s+([^\n]+)",make,re.M)
if not actual_link:
    raise SystemExit("x86 kernel has no actual ld.lld link recipe")
for source in srcs:
    if not (root/source).is_file():
        raise SystemExit(f"missing shared source: {source}")
    obj=defs.get(source)
    if obj is None:
        raise SystemExit(f"x86 kernel missing exact common compilation rule: {source}")
    if f"$(OUT)/{obj}" not in linked.group(1).split():
        raise SystemExit(f"x86 kernel linker omits common object {obj} ({source})")
    if f"$(OUT)/{obj}" not in actual_link.group(1).split():
        raise SystemExit(f"x86 actual ld.lld command omits common object {obj} ({source})")
for port in ("aarch64","riscv64"):
    script=(root/f"scripts/build-{port}-full.sh").read_text()
    if 'scripts/full-kernel-core.sources' not in script or 'for f in $common $arch' not in script:
        raise SystemExit(f"native {port} bypassed shared kernel TU manifest")
    if "kernel/arch/" not in script:
        raise SystemExit(f"native {port} missing actual CPU/MMU providers")
# Ensure syscall-ABI owners remain separate from the historical FS dispatcher.
fs_dispatch = (root/"kernel/fs/syscall.c").read_text()
for path, symbol in (("kernel/fs/namespace_syscall.c", "nas_fs_resolve_path"),
                     ("kernel/fs/socket_syscall.c", "sys_unix_sendmsg")):
    if symbol not in (root/path).read_text():
        raise SystemExit(f"lost dedicated FS owner {path}: {symbol}")
    import re
    if re.search(r"^(?:static\s+)?(?:uint64_t|int64_t|int|bool)\s+"
                 + re.escape(symbol) + r"\s*\(", fs_dispatch, re.M):
        raise SystemExit(f"FS syscall owner definition leaked back into monolith: {symbol}")
# One actual FD entry per descriptor: reject parallel arrays reappearing.
task_h=(root/"kernel/task.h").read_text()
fd_h=(root/"kernel/fd.h").read_text()
assert "struct nas_fd_entry entries[NAS_FD_LIMIT]" in fd_h
assert "fds_owned" not in task_h
for old in ("descriptors[NAS_FD_LIMIT]", "file_slot[NAS_FD_LIMIT]", "fd_cloexec[NAS_FD_LIMIT]"):
    assert old not in task_h, f"parallel descriptor storage returned: {old}"
for name in ("kind", "slot", "cloexec"):
    assert f"uint8_t {name};" in fd_h
for source in (root/"kernel").rglob("*"):
    if source.suffix not in (".c", ".h"): continue
    data=source.read_text()
    if "nas_frame_free" in data:
        raise SystemExit(f"obsolete ambiguous page release API in {source}; use frame_put")
    for old in ("descriptors[", "file_slot[", "fd_cloexec["):
        if old in data: raise SystemExit(f"old FD storage escaped in {source}: {old}")
# M63: path references live ONLY in namespace dentries, not sparse inodes.
tmp_h=(root/"kernel/tmpfs.h").read_text()
tmp_c=(root/"kernel/tmpfs.c").read_text()
tmp_ns=(root/"kernel/fs/tmp_namespace.c").read_text()
if "char path[256];" not in tmp_h or "struct nas_tmp_dentry" not in tmp_h:
    raise SystemExit("lost namespace-owned dentry")
inode_part=tmp_h.split("struct nas_tmp_file {")[1].split("};")[0]
if "char path[" in inode_part or " linked" in inode_part:
    raise SystemExit("name/link boolean leaked back into tmpfs inode")
for text in ("int tmp_link(", "int tmp_rename(", "tmp_child_entry(", "tmp_remove_entry("):
    if text not in tmp_ns: raise SystemExit(f"lost dentry namespace behavior: {text}")
for text in ("int tmp_link(", "int tmp_rename(", "struct nas_tmp_file *tmp_find("):
    if text in tmp_c: raise SystemExit(f"inode backend acquired pathname owner: {text}")
# M64: one per-thread wait snapshot owns all parked syscall readiness state.
task_wait=(root/"kernel/task/wait.c").read_text()
sched=(root/"kernel/task/sched.c").read_text()
assert "struct nas_task_wait wait;" in task_h
assert "nas_task_wait_reset(child);" in (root/"kernel/task.c").read_text()
assert "bool nas_task_wait_ready(" in task_wait
assert "nas_task_wait_ready(" in (root/"kernel/task/sched_core.c").read_text()
for old in ("static bool task_io_ready(", "static bool task_wait_ready(",
            "static bool task_can_wake("):
    if old in sched: raise SystemExit(f"scheduler still owns device wake policy: {old}")
# M89: blocked I/O and epoll use an open-description operation rather
# than hardcoded pipe/socket/device kinds. A new provider must not cause
# another scheduler or epoll backend-specific switch.
epoll=(root/"kernel/fs/epoll.c").read_text()
fsops=(root/"kernel/fs/file_ops.h").read_text()
file_io=(root/"kernel/fs/file_io.c").read_text()
for name,body in (("blocked I/O",task_wait),("epoll",epoll)):
    if "nas_fs_poll_description(" not in body:
        raise SystemExit(f"{name} lost description-owned readiness")
if "switch(w->kind)" in epoll or "nas_pipe_poll_readable(" in epoll:
    raise SystemExit("epoll reintroduced backend-kind readiness switch")
if "poll_description" not in fsops or "nas_fs_poll_description(" not in file_io:
    raise SystemExit("lost open-description readiness operation")
# M90: wait ownership and poll/select/epoll arming use one generic
# description-slot subscription; backend callbacks may not register waiters.
poll=(root/"kernel/fs/poll_syscall.c").read_text()
wait_header=(root/"kernel/task/wait.h").read_text()
fs_syscall=(root/"kernel/fs/syscall.c").read_text()
if "nas_wait_io_arm(" not in wait_header or "nas_wait_io_arm(" not in poll or "nas_wait_io_arm(" not in epoll:
    raise SystemExit("generic Task-owned multi-fd wait interest lost")
if "nas_fs_poll_description(" not in poll or "poll_arm" in fsops or ".poll_arm=" in fs_syscall:
    raise SystemExit("FS reintroduced backend-specific poll wait registration")
# M91: all three kernel assembly context-switch providers are explicitly
# linked in production ELF, and a real QEMU boot exercises stack resume.
for arch in ("x86_64","riscv64","aarch64"):
    asm=root/f"kernel/arch/{arch}/kernel_switch.S"
    if not asm.is_file() or "nas_arch_kernel_swap:" not in asm.read_text():
        raise SystemExit(f"missing native kernel context switch on {arch}")
for needle in ("$(OUT)/arch_kernel_switch.o", "$(OUT)/task_kernel_context.o"):
    if needle not in linked.group(1).split() or needle not in actual_link.group(1).split():
        raise SystemExit(f"x86 kernel fails to LINK continuation {needle}")
for arch in ("riscv64","aarch64"):
    script=(root/f"scripts/build-{arch}-full.sh").read_text()
    for needle in (f"kernel/arch/{arch}/kernel_switch.S",'"$out/kernel_switch.o"'):
        if needle not in script: raise SystemExit(f"{arch} kernel omits continuation {needle}")
if "nas_wait_queue_wake(&wait_test_queue" not in (root/"kernel/task/kernel_context.c").read_text():
    raise SystemExit("native boot lost M92 real continuation wake test")
if "nas_kernel_execution_reset(&child->kernel_exec);" not in (root/"kernel/task.c").read_text():
    raise SystemExit("fork must not inherit a queued kernel wait entry")
if "nas_futex_wait_arm(nas_current_task)" not in (root/"kernel/task/process.c").read_text():
    raise SystemExit("futex syscall is not using generic kernel wait queues")
if "nas_futex_wait_cancel(task)" not in (root/"kernel/task/blocked_completion.c").read_text():
    raise SystemExit("legacy blocked completion must release futex waiter exactly once")
if "task_replay(" in sched:
    raise SystemExit("syscall replay leaked back into scheduler core")
if sched.count("sched_prepare_candidate(")!=3 or sched.count("nas_blocked_complete(next,result)")!=1:
    raise SystemExit("IRQ and syscall scheduling bypassed the single candidate activation boundary")
if "nas_task_wake_ready(t)" not in (root/"kernel/task/futex_wait.c").read_text():
    raise SystemExit("futex producer no longer publishes runnable tasks")
if sched.count("nas_sched_service_kernel();")!=2:
    raise SystemExit("both syscall and timer scheduler must service native kernel workers")
if sched.count("nas_sched_scan_next(&scan,")!=2:
    raise SystemExit("IRQ and syscall task selection bypassed shared scheduler walk")
if "-include $(wildcard $(OUT)/*.d)" not in make:
    raise SystemExit("kernel build must track dependency files for every generated object")
if sched.count("nas_sched_user_commit(")!=2:
    raise SystemExit("IRQ and syscall Task state publication diverged")
if "nas_sched_allow_kernel_slice(" not in (root/"kernel/task/scheduler_service.c").read_text():
    raise SystemExit("kernel worker dispatch bypassed shared domain fairness policy")
if "nas_sched_service_kernel();" not in sched:
    raise SystemExit("syscall and timer schedule stopped servicing native kernel waits")
if "nas_kthread_timers_tick" not in (root/"kernel/task/scheduler_service.c").read_text():
    raise SystemExit("native kernel timeout progress not integrated with scheduler")
if "sync_boot_test();" not in (root/"kernel/task/kthread_boot.c").read_text():
    raise SystemExit("real native kernel sync producer/consumer is not boot-tested")
if "nas_kthread_park_until(&sem->waiters" not in (root/"kernel/task/k_sync.c").read_text():
    raise SystemExit("kernel semaphore bypassed common native wait queue")
if "scheduled_after_user_entry" not in (root/"kernel/task/kthread_boot.c").read_text():
    raise SystemExit("native kernel worker is not resumed by real user scheduler")
tty=(root/"kernel/drivers/tty.c").read_text()
if "nas_mmu_user_writable(" in tty or "nas_user_address_writable(dst+i)" not in tty:
    raise SystemExit("TTY is checking hardware PTE WRITE instead of the logical COW VMA right")
cow=(root/"kernel/mm/mm.c").read_text()
if "nas_mm_fork_cow(parent->mm)" not in (root/"kernel/task/process.c").read_text():
    raise SystemExit("fork is not using refcounted, shared-frame private COW")
if "nas_mm_break_cow(mm,addr,vma->prot,false)" not in (root/"kernel/mm/fault.c").read_text():
    raise SystemExit("write faults bypassed MM COW ownership and private copy")
if "nas_mm_break_cow((struct nas_mm *)mm,at,0,true)" not in (root/"kernel/mm/remote_copy.c").read_text():
    raise SystemExit("ptrace silently mutates shared COW frame")
if "nas_frame_get(pa)" not in cow or "cow_protect_parent" not in cow:
    raise SystemExit("MM private fork no longer shares refs and downgrades parent PTEs")
if "nas_kthread_boot_selftest();" not in (root/"kernel/init/main.c").read_text():
    raise SystemExit("three-ISA native boot no longer exercises real kernel workers")
signal_src=(root/"kernel/task/signal.c").read_text()
if "nas_sched_thread_group_exit(0,NAS_SIGSEGV);" not in signal_src or "nas_arch_load_user_frame(nas_current_task,frame);" not in signal_src:
    raise SystemExit("broken userspace sigframe once again panics or fabricates a user return")
if 'nas_fail("M30 cannot build native user signal frame")' in signal_src:
    raise SystemExit("kernel-wide panic returned for an invalid user signal stack")
if "nas_kernel_context_boot_selftest();" not in (root/"kernel/init/main.c").read_text():
    raise SystemExit("native QEMU kernels no longer test real kernel C resume")
print(f"[M50 ARCH PASS] all {len(srcs)} actual shared kernel TUs are linked on x86 "
      "and built on RV64/AArch64; hardware providers remain ISA-specific")
