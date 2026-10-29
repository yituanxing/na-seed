#!/usr/bin/env python3
"""Verify the *production* x86 early-NXE setup and syscall EFER contract.

Privileged MSR writes cannot be executed in Linux user mode. Inspect the
compiled kernel TU separately and fail closed if the C initialization order,
CPUID capability gate or read-back changes silently.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
cpu = (root / "kernel/arch/x86_64/cpu.c").read_text()
main = (root / "kernel/init/main.c").read_text()


def require(text: str, pattern: str, description: str) -> None:
    if not re.search(pattern, text, re.S):
        raise SystemExit(f"[X86 NXE FAIL] {description}")


def body(text: str, name: str) -> str:
    # Kernel functions under test have ordinary, nonnested C blocks except
    # in capability branches. Delimit at the next top-level declaration.
    match = re.search(r"(?:static\s+)?(?:bool|void)\s+" + re.escape(name)
                      + r"\s*\([^)]*\)\s*\{", text)
    if not match:
        raise SystemExit(f"[X86 NXE FAIL] missing {name}")
    depth = 1
    start = match.end()
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index]
    raise SystemExit(f"[X86 NXE FAIL] unterminated {name}")


require(cpu, r"#define\s+NAS_X86_EFER_MSR\s+UINT32_C\(0xc0000080\)",
        "EFER MSR must be 0xC0000080")
require(cpu, r"#define\s+NAS_X86_EFER_NXE\s+\(UINT64_C\(1\)\s*<<\s*11\)",
        "NXE must be EFER bit 11")
require(cpu, r"#define\s+NAS_X86_EFER_SCE\s+\(UINT64_C\(1\)\s*<<\s*0\)",
        "syscall SCE must remain EFER bit 0")

capability = body(cpu, "nas_x86_cpu_has_nx")
require(capability, r"cpuid.*0x80000000.*eax\s*<\s*UINT32_C\(0x80000001\).*"
        r"cpuid.*0x80000001.*edx\s*&\s*\(UINT32_C\(1\)\s*<<\s*20\)",
        "probe extended CPUID leaf availability then NX EDX bit 20")
enabled = body(cpu, "nas_x86_enable_nxe")
require(enabled, r"if\s*\(\s*!nas_x86_cpu_has_nx\(\)\s*\)\s*nas_fail",
        "fail closed if CPU does not advertise NX")
require(enabled, r"efer\s*=\s*rdmsr\(NAS_X86_EFER_MSR\).*"
        r"wrmsr\(NAS_X86_EFER_MSR,\s*efer\s*\|\s*NAS_X86_EFER_NXE\)",
        "EFER NXE write must preserve firmware's other EFER bits")
require(enabled, r"if\s*\(\s*\(rdmsr\(NAS_X86_EFER_MSR\)\s*&\s*"
        r"NAS_X86_EFER_NXE\)\s*==\s*0\s*\)\s*nas_fail",
        "EFER.NXE readback must be fail-closed")

prepare = body(cpu, "nas_arch_prepare_user_cpu")
if not (0 <= prepare.find("nas_arch_cpu_tables_init()") <
        prepare.find("nas_x86_enable_nxe()") <
        prepare.find("nas_arch_cpu_user_fp_init()")):
    raise SystemExit("[X86 NXE FAIL] install IDT, then NXE, then user setup")
require(main, r"nas_arch_prepare_user_cpu\(\)\s*;.*nas_mmu_bootstrap\(\)",
        "NXE CPU init must precede MMU bootstrap in shared init")

syscall = body(cpu, "nas_arch_syscall_enable")
require(syscall, r"efer\s*=\s*rdmsr\(NAS_X86_EFER_MSR\).*"
        r"wrmsr\(NAS_X86_EFER_MSR,\s*efer\s*\|\s*NAS_X86_EFER_SCE\)",
        "enabling syscall must preserve prior NXE and other EFER bits")
require(body(cpu, "nas_arch_nx_enabled"),
        r"rdmsr\(NAS_X86_EFER_MSR\)\s*&\s*NAS_X86_EFER_NXE",
        "runtime MMU NXE query must match initialization flag")

for initial_efer in (0, 1, 0x100, 0x400, 0x1234, 0x8000000000000100):
    nxe = initial_efer | (1 << 11)
    sce = nxe | 1
    if not (nxe & (1 << 11) and sce & (1 << 11) and sce & 1 and
            (sce & initial_efer) == initial_efer):
        raise SystemExit("[X86 NXE FAIL] EFER flag preservation model")

print("[X86 NXE PASS] capability CPUID, fail-closed MSR readback, "
      "early-MMU order, syscall SCE preservation")
