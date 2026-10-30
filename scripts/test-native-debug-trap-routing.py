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

for arch, code, breakpoint, return_reg in (
    ("riscv64", rv, "scause==3U", "frame->x[10]=resumed"),
    ("aarch64", arm, "(esr>>26)==0x3cULL", "frame->x[0]=resumed"),
):
    assert breakpoint in code, arch
    assert "bool traced=nas_ptrace_user_exception(tracee,NAS_SIGTRAP" in code, arch
    assert "if(!traced)nas_sched_thread_group_exit(0,NAS_SIGTRAP);" in code, arch
    assert "nas_task_schedule((uint64_t *)(void *)frame)" in code, arch
    assert return_reg in code, arch

print("[NATIVE DEBUG ROUTE PASS] 3 ISA traced stop retained; untraced SIGTRAP exit guarded")
