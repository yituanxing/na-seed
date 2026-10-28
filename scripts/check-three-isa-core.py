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
assert len(srcs)==len(set(srcs))==64, "common kernel TU manifest drift"
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
# Block layer/ramdisk must be linked on all ISAs and remain kernel only.
for source in ("kernel/block/device.c", "kernel/block/ramdisk.c"):
    data=(root/source).read_text()
    for forbidden in ("user_byte(", "nas_user_write_bytes(", "__NR_", "nas_fs_syscall("):
        if forbidden in data:
            raise SystemExit(f"BlockDevice acquired syscall/user address policy: {source}")
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
# One shared VFS namei owner: no lexical path resolver may erase a
# regular-file parent or a symlink before the component walker sees it.
lookup=(root/"kernel/fs/lookup.c").read_text()
namespace=(root/"kernel/fs/namespace_syscall.c").read_text()
vfs_src=(root/"kernel/fs/vfs.c").read_text()
if not all(symbol in lookup for symbol in (
        "int nas_fs_lookup_parent(", "int nas_fs_canonical_candidate(",
        "int nas_vfs_join_child(")):
    raise SystemExit("shared namespace parent/candidate/leaf owner missing")
if ("nas_fs_canonical_candidate(raw,out)" not in namespace or
        "static bool has_dot_component(" in namespace or
        "static int vfs_parent(" in vfs_src or
        "int nas_path_resolve(" in (root/"kernel/path.c").read_text()):
    raise SystemExit("old lexical namespace walker returned")
for source in ("kernel/fs/vfs_providers.c", "ports/littlefs/vfs_port.c",
               "ports/lwext4/guest_vfs.c"):
    if "nas_vfs_join_child(" not in (root/source).read_text():
        raise SystemExit(f"provider duplicates leaf join: {source}")
vfs_providers=(root/"kernel/fs/vfs_providers.c").read_text()
# Stable VFS mount storage: no source-owned registration pointers are
# used as live File mount identities. The table is sparse, and removing one
# mount must not shift the superblock address of another pinned File.
vfs_src=(root/"kernel/fs/vfs.c").read_text()
for item in ("struct nas_vfs_mount superblock;", "const struct nas_vfs_mount *registration;",
             "mounts[slot].superblock.mountpoint=mounts[slot].path;",
             "mounts[at]=(struct mount_slot){0};",
             "parent->mount_id!=slot->parent_mount_id"):
    if item not in vfs_src:
        raise SystemExit(f"unstable VFS-owned mount/parent identity: {item}")
if "mounts[i]=mounts[i+1]" in vfs_src or "mounts[count++]" in vfs_src:
    raise SystemExit("packed mount registry would invalidate open File mounts")
# A pinned empty ext4 directory is safely detached, never recursively
# removed before its last owned File/cwd inode lease reaches zero.
if "unlink_open_dir_safe" not in (root/"kernel/fs/vfs.h").read_text() or \
   ".unlink_open_dir_safe=true" not in (root/"ports/lwext4/guest_vfs.c").read_text() or \
   "ext_stage_orphan(ctx,path,number,true)" not in (root/"ports/lwext4/guest_vfs.c").read_text():
    raise SystemExit("ext4 opened-directory rmdir must defer inode destruction")
# The original ext4 port may NOT silently treat a directory rename
# replacement as a file truncate. All ordinary and empty-directory
# victims use one typed staged-inode protocol with a safe emptiness check.
ext4_port=(root/"ports/lwext4/guest_vfs.c").read_text()
for token in ("ext_dir_empty(", "ext_remove_staged(", "ext_move_staged(",
              "ext_stage_orphan(", "name[0]=='d'", "nonempty?-ENOTEMPTY:0"):
    if token not in ext4_port:
        raise SystemExit(f"ext4 typed directory-orphan contract missing: {token}")
if ".rename_replace_open_safe=true" not in ext4_port or \
   "ext4_dir_rm(hidden)" in ext4_port:
    raise SystemExit("unsafe ext4 open-directory replace lifecycle")
# Borrowed inode identity is never trusted solely by its pointer. Mounted
# VFS validation must check the live mount registry before dereferencing
# a possibly retired descriptor; tmpfs checks its monotonic inode number.
if "validate_inode" not in (root/"kernel/fs/vfs.h").read_text() or \
   "tmp_validate_inode(" not in vfs_providers or \
   ".validate_inode=tmp_validate_inode" not in vfs_providers or \
   "if(node->filesystem!=node->mount->type)return -EINVAL;" not in vfs_src:
    raise SystemExit("VFS inode-incarnation validator or safe mount boundary missing")
# The ext4 last-link orphan protocol is entirely within the upstream
# adapter and VFS inode lifetime contract, not a vendor fork. A readable,
# writable, unlink-open File must retain its data until final unpin.
vfs_h=(root/"kernel/fs/vfs.h").read_text()
ext4_port=(root/"ports/lwext4/guest_vfs.c").read_text()
if "unlink_open_file_safe" not in vfs_h or \
   "nas_vfs_node_has_lease(" not in (root/"kernel/fs/vfs.c").read_text() or \
   "ext_stage_orphan(" not in ext4_port or \
   "ext_last_inode_unpin(" not in ext4_port or \
   "ext_orphan_prepare(" not in ext4_port or \
   ".unlink_open_file_safe=true" not in ext4_port or \
   ".rename_replace_open_safe=true" not in ext4_port or \
   "ext_move_staged(hidden,to,is_dir)" not in ext4_port or \
   "EXT4_ORPHAN_DIRECTORY" not in ext4_port:
    raise SystemExit("ext4 opened-last-link VFS orphan protocol incomplete")
# M63: path references live ONLY in namespace dentries, not sparse inodes.
tmp_h=(root/"kernel/tmpfs.h").read_text()
tmp_c=(root/"kernel/tmpfs.c").read_text()
tmp_ns=(root/"kernel/fs/tmp_namespace.c").read_text()
if "struct nas_tmp_dentry" not in tmp_h or \
   "const void *parent_inode;" not in tmp_h or "char name[256];" not in tmp_h:
    raise SystemExit("tmpfs must own a parent-inode + one-basename dentry")
if "char path[256];" in tmp_h:
    raise SystemExit("absolute path cache reintroduced into tmpfs dentry")
if "tmp_parent(" in tmp_h or "bool tmp_parent(" in tmp_ns:
    raise SystemExit("dead second tmpfs lexical parent helper returned")
for required in ("tmp_create_at(", "tmp_mkdir_at(", "tmp_unlink_at(",
                 "tmp_rename_at(", "tmp_link_at(", "tmp_symlink_at(",
                 "tmp_child_entry_inode(", "tmp_dir_parent_inode("):
    if required not in vfs_providers:
        raise SystemExit(f"tmpfs VFS provider bypassed parent inode: {required}")
if "tmp_child_path(" in vfs_providers:
    raise SystemExit("tmpfs reopened old string-based namespace mutations")
if "source->parent_inode=new_parent;" not in tmp_ns or \
   "source->name" not in tmp_ns or "d->path" in tmp_ns:
    raise SystemExit("tmpfs directory rename must update one dentry edge")
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
x86_context=(root/"kernel/arch/x86_64/context.c").read_text()
x86_entry=(root/"kernel/arch/x86_64/entry.S").read_text()
# Accept only the established CPU scratch/ABI fallback or the stricter
# native continuation contract: syscall_entry constructs RIP/CS/FLAGS/RSP/SS
# on its own Task entry stack BEFORE entering resumable kernel C code.
# The user RCX/R11 saved as GPRs are NEVER substitutes for IRET metadata.
original_pc=("task->arch.resume_user_rip = nas_x86_syscall_return_rip;" in x86_context and
             "task->arch.resume_user_rflags = nas_x86_syscall_return_flags;" in x86_context)
pinned_pc=all(needle in x86_context for needle in (
    "frame[16]==0x33U && frame[19]==0x2bU",
    "task->arch.resume_user_rip=frame[15];",
    "task->arch.resume_user_rflags=frame[17];",
    "task->arch.resume_user_rsp=frame[18];")) and all(
    needle in x86_entry for needle in (
        "movq %rax, 120(%rsp)",
        "movq $0x33, 128(%rsp)",
        "movq %rax, 136(%rsp)",
        "movq %rax, 144(%rsp)",
        "movq $0x2b, 152(%rsp)"))
if not (original_pc or pinned_pc):
    raise SystemExit("x86 IRET PC/RFLAGS must use the Task's own frame or explicit CPU scratch state")
if "resume_user_rip=frame[7]" in x86_context or "resume_user_rflags=frame[8]" in x86_context:
    raise SystemExit("x86 IRET metadata incorrectly decoded from user RCX/R11")
x86_return=(root/"kernel/arch/x86_64/entry.S").read_text()
if x86_return.count("movq nas_x86_syscall_return_rip(%rip), %rax")!=1:
    raise SystemExit("syscall epilogue must not re-read another Task's global IRET PC")
if "frame[15]=task->arch.resume_user_rip;" not in x86_context or \
   "frame[17]=task->arch.resume_user_rflags;" not in x86_context or \
   "frame[18]=task->arch.resume_user_rsp;" not in x86_context:
    raise SystemExit("native x86 signal/ptrace return did not restore Task-owned IRET")
if "void nas_sched_preempt_select(void)" not in sched:
    raise SystemExit("native scheduler still exposes a foreign-Task IRQ frame copy API")
if "native_irq_preempt_source=self;" not in sched or \
   "interrupted!=next" not in sched or \
   'nas_print("[M28] timer-preempt from=")' not in sched:
    raise SystemExit("M28 native IRQ acceptance must prove actual distinct-Task preemption")
if "nas_arch_kernel_swap(&self->kernel_exec.cpu,&scheduler_cpu)" not in sched:
    raise SystemExit("Task does not park its native C continuation")
if "nas_arch_kernel_swap(&scheduler_cpu,&next->kernel_exec.cpu)" not in sched:
    raise SystemExit("native dispatcher does not resume Task C frames")
completion=(root/"kernel/task/blocked_completion.c").read_text()
if "task_retry_in_resumed_context" in completion or "task_replay(" in completion:
    raise SystemExit("legacy syscall dispatch switch survived native C continuation")
if "return task->wait.finish(task);" not in completion or "self->wait.finish=finish;" not in sched:
    raise SystemExit("resumed kernel C stack does not invoke owning syscall provider")
if "finish_io_provider(" not in (root/"kernel/syscall/dispatch.c").read_text():
    raise SystemExit("blocking I/O providers must own their retry, not Task scheduler")
if "nas_sched_scan_next(&scan,&slot)" not in sched or \
   "nas_sched_scan_init(&scan,native_cursor)" not in sched:
    raise SystemExit("native scheduler bypasses canonical complete-ring scan")
sched_core=(root/"kernel/task/sched_core.c").read_text()
sched_core_h=(root/"kernel/task/sched_core.h").read_text()
if "nas_sched_user_commit" in sched_core or "nas_sched_user_commit" in sched_core_h or \
   "visit_limit" in sched_core or "scan->exclude" in sched_core or \
   "task_replay(" in sched:
    raise SystemExit("obsolete frame-copy commit or partial/excluded scan returned")
if "nas_task_wait_needs_idle_poll" in sched or \
   "nas_task_wait_has_external_wake_source" in sched or \
   "nas_task_wait_has_external_wake_source" in task_wait or \
   "native scheduler deadlock" in sched or \
   "nas_kthread_deadlines_armed(" in sched:

    raise SystemExit("obsolete syscall-based idle deadlock gate returned")
# IRQ-masked busy polling is not an acceptable native idle implementation.
# Require ISA-specific privileged interrupt handling, never a user-only IRQ
# path or an unconditional WFI/HLT executed with interrupts still disabled.
if "nas_arch_idle_wait();" not in sched or "void nas_arch_idle_wait(void);" not in (root/"kernel/arch/arch.h").read_text():
    raise SystemExit("native scheduler lost shared IRQ-aware idle boundary")
task_src=(root/"kernel/task.c").read_text()
release=task_src.split("void nas_task_release(",1)[1].split("struct nas_task *nas_task_by_pid",1)[0]
if "task->native_trap_frame=0;" not in release or "task->resume_valid=false;" not in release:
    raise SystemExit("retired Task still exposes a parked user/kernel continuation")
if "child->ppid = nas_current_task->tgid;" not in task_src:
    raise SystemExit("worker fork silently reintroduced TID-based process parenthood")
if "struct nas_task *init=nas_task_by_pid(1);" not in task_src:
    raise SystemExit("orphan reparenting assumes PID 1 never execs from worker")
if "native_reap_exited_threads();" not in sched or \
   "nas_arch_restore_cpu(init);" not in sched or \
   "dead->state==NAS_TASK_ZOMBIE && !dead->ptrace_tracer_pid" not in sched:
    raise SystemExit("exited pthread Task/MM lifetime not reclaimed from scheduler stack")
dispatch=(root/"kernel/syscall/dispatch.c").read_text()
if "const bool init_shutdown =" in dispatch or \
   "nas_sched_child_exit((uint32_t)args[1],0)" not in dispatch or \
   "nas_sched_thread_group_exit((uint32_t)args[1],0)" not in dispatch:
    raise SystemExit("init worker/leader syscall exits bypass Task group lifetime")
if "nas_init_process_terminated(child->exit_code,child->termination_signal)" not in sched or \
   "nas_init_process_terminated(uint32_t code,uint32_t signal)" not in (root/"kernel/init/main.c").read_text():
    raise SystemExit("PID1 boot policy observer bypasses group-finalization contract")
process_src=(root/"kernel/task/process.c").read_text()
wait_src=(root/"kernel/task/wait.c").read_text()
fd_src=(root/"kernel/fs/syscall.c").read_text()
socket_src=(root/"kernel/fs/unix_socket.c").read_text()
if "nas_process_wait_owner(child,nas_current_task)" not in process_src or \
   "nas_process_wait_owner(child,task)" not in wait_src or \
   "nas_task_process_pid(waiter)" not in process_src:
    raise SystemExit("parent TGID and ptrace TID wait owner drifted apart")
if "nas_locks_apply(&inode->posix_locks,TASK.pid" in fd_src:
    raise SystemExit("POSIX locks owned by worker TID rather than group")
if "nas_fs_drop_kernel_description(pending);" not in socket_src or \
   "nas_fs_drop_kernel_description(held.slots[i]);" not in socket_src:
    raise SystemExit("AF_UNIX pending accept or SCM_RIGHTS bypasses finalizer")
if "if(child->is_thread) {" not in (root/"kernel/task/process.c").read_text():
    raise SystemExit("ptrace wait4 cannot reap a consumed nonleader exit")
x86_timer=(root/"kernel/arch/x86_64/timer.c").read_text()
if 'sti; hlt; cli' not in x86_timer or 'if ((frame->cs & 3U) == 0U)' not in x86_timer or    'jz nas_x86_iret_restore' not in x86_entry:
    raise SystemExit("x86 kernel PIT idle IRQ must restore CPL0 IRET without user signals")
rv_timer=(root/"kernel/arch/riscv64/timer.c").read_text()
rv_trap=(root/"kernel/arch/riscv64/trap.S").read_text()
if not all(s in rv_timer for s in ('nas_rv_idle_trap', 'csrsi sstatus,2', 'csrci sstatus,2', 'nas_rv_idle_irq_c')) or    'nas_rv_idle_trap:' not in rv_trap or 'call nas_rv_idle_irq_c' not in rv_trap:
    raise SystemExit("RV64 idle must handle S-mode IRQ without sscratch stack swap")
arm_timer=(root/"kernel/arch/aarch64/timer.c").read_text()
arm_trap=(root/"kernel/arch/aarch64/trap.S").read_text()
if not all(s in arm_timer for s in ('msr daifclr,#2', 'msr daifset,#2', 'nas_arm_timer_idle_irq_c')) or    'b nas_arm_upper_irq' not in arm_trap or 'nas_arm_upper_irq:' not in arm_trap or    'b nas_arm_restore_frame' not in arm_trap or \
   'if((id & 0x3ffU)==1023U)return 0;' not in arm_timer:
    raise SystemExit("ARM64 idle needs EL1h IRQ entry with in-place kernel ERET")
# Both ARM64 exception classes need separate return boundaries: EL0 timer
# resumes the Task's canonical frame, EL1h idle resumes its kernel frame.
if 'bl nas_arm_lower_irq_c\n    b nas_arm_trap_return' not in arm_trap:
    raise SystemExit("ARM64 EL0 timer IRQ incorrectly falls into EL1h idle vector")
arm_context=(root/"kernel/arch/aarch64/context.c").read_text()
if 'source->spsr_el1 & 15U' not in arm_context:
    raise SystemExit("ARM64 user-return stack has no EL0 privilege guard")
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
if "nas_sched_thread_group_exit(0,NAS_SIGSEGV);" not in signal_src or 'nas_fail("fatal Task resumed after thread-group exit")' not in signal_src:
    raise SystemExit("broken userspace sigframe once again panics or fabricates a user return")
if 'nas_fail("M30 cannot build native user signal frame")' in signal_src:
    raise SystemExit("kernel-wide panic returned for an invalid user signal stack")
if "nas_kernel_context_boot_selftest();" not in (root/"kernel/init/main.c").read_text():
    raise SystemExit("native QEMU kernels no longer test real kernel C resume")
print(f"[M50 ARCH PASS] all {len(srcs)} actual shared kernel TUs are linked on x86 "
      "and built on RV64/AArch64; hardware providers remain ISA-specific")
