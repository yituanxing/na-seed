#!/usr/bin/env python3
"""Guard the single native-QEMU runner/log mapping used by M58+ programs.

Each original workload keeps its own guest assertions and previous entrypoint.
This host-only check is intentionally separate from the real QEMU acceptance.
"""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parent.parent
HELPER = "scripts/native-qemu-target.sh"
WRAPPERS = (
    "m58-runtime", "m59-worktree", "m60-terminals",
    "m65-procfs", "m66-monitor", "m67-control", "m69-inspect",
    "m70-vmfd", "m72-strace", "m73-strace", "m74-strace",
    "m75-gdb", "m76-gdb", "m79-workflow",
)
EXPECTED = {
    "amd64": ("scripts/probe-x86-busybox-parity.sh", "build/busybox-source-parity-x86"),
    "riscv64": ("scripts/probe-riscv64-busybox-matrix.sh", "build/port-riscv64/full/matrix-source"),
    "arm64": ("scripts/probe-aarch64-busybox-matrix.sh", "build/port-aarch64/full/matrix-source"),
}


def resolve(port: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["sh", "-c", f'port="{port}"; . {HELPER}; printf "%s\n%s\n" "$runner" "$logs"'],
        cwd=ROOT, text=True, capture_output=True, check=False,
    )


def main() -> None:
    for port, expected in EXPECTED.items():
        result = resolve(port)
        assert result.returncode == 0 and result.stdout.splitlines() == list(expected), (
            port, result.returncode, result.stdout, result.stderr
        )
        assert (ROOT / expected[0]).is_file(), f"missing genuine QEMU runner: {expected[0]}"
    for invalid in ("", "x86_64", "invalid"):
        result = resolve(invalid)
        assert result.returncode == 2 and not result.stdout, (
            invalid, result.returncode, result.stdout
        )
    for suffix in WRAPPERS:
        path = ROOT / f"scripts/probe-{suffix}.sh"
        source = path.read_text()
        assert source.count(f". {HELPER}") == 1, f"{path}: target helper must be sourced once"
        assert "runner=" not in source and "logs=" not in source, (
            f"{path}: stale private runner or serial-path mapping"
        )
        assert 'sh "$runner"' in source, f"{path}: genuine native QEMU execution removed"
        assert 'log="$logs/' in source, f"{path}: raw guest serial verification removed"
    print(f"[NATIVE QEMU TARGET PASS] 3 target mappings, {len(WRAPPERS)} original workloads")


if __name__ == "__main__":
    main()
