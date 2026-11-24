#!/usr/bin/env python3
"""Static guard for existing ptrace stops and newly safe untraced debug traps.

Hardware runtime equivalence still requires three native QEMU guest probes.
"""
from pathlib import Path


def source(path):
    return Path(path).read_text()


x86 = source("kernel/arch/x86_64/cpu.c")
rv = source("kernel/arch/riscv64/context.c")
arm = source("kernel/arch/aarch64/context.c")

# x86 must keep native #DB/#BP debugger stop/resume handling, including the
# debugger-modified register image. The same branch must own an untraced
# SIGTRAP exit rather than falling through to exception_report()/nas_fail().
assert "bool traced=nas_ptrace_user_exception(tracee,signo,si_code" in x86
assert "if(traced || vector==1 || vector==3 || vector==13 ||" in x86
assert "if(!traced)\n                nas_sched_thread_group_exit(0,vector==13 ? 11U : signo);" in x86
assert "if(traced) {\n                nas_arch_load_user_frame" in x86

# RV64 and ARM64 now share the same fatal/traced completion owner.
# Keep both ISA-specific routes, including their exact return register and
# TRAP_TRACE code, and verify the ordered shared completion body.
common = source("kernel/arch/trap_common.h")
assert """if(!nas_ptrace_user_exception(tracee,signo,code,pc,sp,address))
        nas_sched_thread_group_exit(0,signo);
    *return_reg=nas_task_schedule(frame);
    nas_signal_user_return(frame);""" in common

for arch, code, breakpoint, step_code, pc, sp, return_reg in (
    ("riscv64", rv, "scause==3U", "stepped?2:1",
     "frame->sepc", "frame->x[2]", "frame->x[10]"),
    ("aarch64", arm, "(esr>>26)==0x3cULL", "(esr>>26)==0x3cULL?1:2",
     "frame->elr_el1", "frame->sp_el0", "frame->x[0]"),
):
    assert breakpoint in code, arch
    assert '#include "../trap_common.h"' in code, arch
    compact = "".join(code.split())
    assert ("nas_arch_save_user_frame(tracee,0);" in compact
            and "nas_arch_save_fpu(tracee);" in compact), arch
    assert (f"nas_arch_finish_user_exception(tracee,NAS_SIGTRAP,{step_code},"
            f"{pc},{sp},{pc},(uint64_t*)(void*)frame,&{return_reg});") in compact, arch
    assert (f"nas_arch_finish_user_exception(tracee,outcome.signo,outcome.si_code,"
            f"{pc},{sp},signal_addr,(uint64_t*)(void*)frame,&{return_reg});") in compact, arch

print("[NATIVE DEBUG ROUTE PASS] 3 ISA traced stop retained; untraced SIGTRAP exit guarded")
