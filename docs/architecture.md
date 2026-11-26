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

## Retired parallel M0 kernel

The obsolete `NASEED_PORT_M0` RISC-V/AArch64 miniature boot/test stack, its standalone ELF loader and page-table smoke tests have been removed. All three targets now build the shared kernel and native user-space sample through the same maintained build route. Removing the old internal tests does not prove complete ABI or device correctness; the build and x86 UEFI boot matrix is the current safety gate.

## Targeted host regression

Run `make check-host` to compile the real framebuffer/display implementation
and check mode ABI, boot handoff, device takeover, byte-range bounds, VMA
owner pins, and detach/rebind. This restores focused coverage lost when the
historical host-only framebuffer test was deleted. It does not emulate a GPU.

## VMA contract

`make check-host` also compiles the production `anon_regions.c` with only heap,
file-reference and device callbacks stubbed. It verifies VMA gap selection,
coalescing, private-file offsets after protect/split, MM clone ownership,
failed backing-pin rollback and DMA-device pin balances. Old, uncalled M11
anonymous overlay/record compatibility wrappers have been retired; all live
callers use the typed VMA interface.

## Truncate and COW protection

The host contract `tests/vm-truncate-contract.c` links the production VM
truncate walker with fake page-table, inode-match and frame interfaces.
Truncating a file invalidates resident shared cache mappings and private
COW leaves crossing the new EOF. A private frame may still be held by a forked
process; it must be **unmapped and frame_put**, never zeroed in place. Private
pages entirely before EOF remain untouched. Absent private pages fault via the
pinned inode and reflect its new EOF. Run via `make check-host`.

Truncate walks only the boundary page and suffix of each mapped file window,
computed using that VMA's actual split-adjusted `file_offset`. The host
contract checks a large sparse VMA and nonzero-offset shared/private maps,
including exactly one page-table probe at a partial EOF.

## File size change and resident COW mappings

VFS invokes the MM's truncation invalidation only for a **real shrink**.
An equal-size `ftruncate`, sparse extension or successful growing `fallocate`
must not discard already resident private-COW PTEs. File providers are
responsible for zeroing newly visible bytes; the MM still drops affected
leaves when the new EOF actually moves backwards.

## tmpfs provider size contract

`tests/tmpfs-size-contract.c` compiles the real `kernel/tmpfs.c` with a
refcounted in-memory PMM. It verifies sparse growth, the zeroing of bytes
written through shared mappings beyond EOF before they become visible,
normal and KEEP_SIZE fallocate, quota rollback and delayed final frame
reclamation while a shared page still owns an independent reference.
