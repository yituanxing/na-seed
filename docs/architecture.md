# NaSeed source and verification boundaries

## Source ownership

- `kernel/`: kernel implementation, device drivers, virtual memory, scheduler, VFS, architecture backends.
- `ports/`: integration adapters to upstream libraries and hardware stacks.
- `third_party/`: pinned external code and Git submodules; do not rewrite casually.
- `user/`: native sample user programs and link scripts.
- `Makefile`: primary build graph. `scripts/build-native-full.sh` and `scripts/full-kernel-core.sources` own RV64/AArch64 source selection. The three optional filesystem-link scripts remain build-time dependencies.

## Verification boundary

1. x86-64 ELF link must pass, and the primary Limine UEFI ISO must build.
2. RV64 and AArch64 native full-kernel and sample-user ELF links must pass.
3. x86 QEMU must reach both M0 serial markers and exit through isa-debug-exit.
4. Older H0-H15 and M-number scenario suites are intentionally not part of the baseline. Their removal neither fixes nor verifies the underlying drivers and syscalls.
5. Reintroduce hardware and distribution integration tests only after a feature has a maintained build path and a focused observable contract. Avoid duplicating transitive test suites.

This reset replaces the historical test forest. It does not delete the kernel and is not a claim of regression-free operation.
