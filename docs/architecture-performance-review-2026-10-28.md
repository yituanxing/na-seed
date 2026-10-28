# NaSeed architecture/performance checkpoint — 2026-10-28

**Exact starting tree:** `vfs-block-lwext4-foundation` at `fbe05b1`
(PR #146; not `main` at `9bed1c3`). **Reference implementations:**
NaOS `aether-os-studio/na-kernel` at `2c45649`, and Linux upstream
`fs/namei.c`, `fs/dcache.c`, `fs/namespace.c` plus
`Documentation/filesystems/path-lookup.rst`. These are ownership/lookup
comparators, not code to import or permission to introduce SMP locking now.

## Architectural baseline to preserve

| Owner | Present boundary | Rule for performance changes |
| --- | --- | --- |
| Linux ABI | `kernel/syscall`, `kernel/fs/*syscall.c` | Decode userspace ABI; do not introduce filesystem-specific syscall shortcuts. |
| Task/File | `kernel/task`, `kernel/fd.c` | Shared open-file descriptions, FS/cwd and MM have independent reference lifetimes. |
| VM | `kernel/mm/mm.c`, `vm.c`, `fault.c`, `usercopy.c` | One shared COW/fault implementation across three ISAs; permission and rollback semantics precede throughput. |
| Namespace | `kernel/fs/lookup.c`, `vfs.c` | Exactly one symlink/dot/namei algorithm; Dentry/Path owns names, leases and mount identity. |
| Providers | `kernel/fs/vfs_providers.c`, `ports/*` | Backend receives parent inode plus basename; no direct Linux ABI, bus or user pointer. |
| Storage | `kernel/block`, `kernel/drivers/{core,port,virtio}`, `ports/uboot` | Provider -> BlockDevice -> driver/transport; read/write/flush and mount references have explicit errors. |
| ISA | `kernel/arch/{x86_64,riscv64,aarch64}` | Architecture-specific MMU, traps and hardware only; shared C code remains shared. |

NaOS already has a refcounted `vfs_path`, dcache hash buckets and explicit
mount topology. Linux's path lookup explicitly separates Dentry identity,
inode identity and mount traversal. NaSeed has a positive Dentry graph and
stable mount slots today, but is **not** yet Linux's VFS: path binding still
re-resolves canonical components, mount crosses still scan the mount table,
mounts are not a fully independent `(vfsmount, dentry)` path object, and
negative Dentry caching is not implemented. Keep these gaps explicit.

## First evidence-driven optimization in this branch

`nas_vfs_path_bind()` used to resolve `/`, then each growing prefix with
`nas_fs_lookup_node(prefix)`. For a path with *d* components, that may
revisit up to `1 + 2 + ... + d` provider edges **after** the caller has
already run namei. Rebuilding a Dentry graph should not restart a namei
walk at every prefix.

The change binds each verified canonical component through the existing
`nas_vfs_lookup_step()` and the same refcounted `dentry_acquire()`.
It also rejects symlink components to avoid treating an alias spelling as
canonical. No provider API, syscall ABI, independent mount ownership,
locking or per-ISA code was added.

The real compiled host VFS fixture instrumented `bootfs_find()` calls
inside a single `nas_vfs_path_bind("/bin/demo")`:

| Same operation | Before | After |
| --- | ---: | ---: |
| Actual provider root/child probes | 6 | 3 |

This is a **provider-probe count**, not a claimed 2x full-kernel speedup.
Normal file opens primarily do a namei without Path binding, so a guest
GCC/Kbuild wall-time improvement is **not yet established**. It removes
quadratic repeated work in the directory/Path lifecycle while preserving
existing mount/alias behavior. The regression asserts the exact probe
count and rejects a direct symlink alias.

## Evidence already exists for genuine larger programs

- M55 previously diagnosed an original GCC/ld relink slowdown caused by
  unnecessary static musl relinking during a real cJSON CMake/Ninja build.
  This is a build graph/link-mode problem, **not** an MM deadlock.
- The existing shared per-page usercopy change had a measured RV64 M38
  original-GCC/QEMU A/B of `69.5s -> 15.0s` (4.63x) on that particular
  workload. This must not be generalized to every workload or ISA.
- M50 pull-request fast mode runs 12 original prebuilt/ABI workloads per
  ISA. The 17-case full matrix, including five genuine in-guest source
  builds, is opt-in; M56 in-guest original BusyBox Kbuild is a separate
  dispatch-only three-ISA workflow. This separation is intentional and
  should not be weakened to make the dashboard look fast.

## Next real-workload performance gate, before large refactors

1. **Pin one repeatable workload:** original Alpine GCC compiling/linking
   a representative C project, then original BusyBox Kbuild. Capture
   exact tree, QEMU invocation, ISA, image, compiler flags and cache state.
2. **Separate clocks:** host provisioning/build time, QEMU boot time,
   guest source extract/configure/compile/link/execute time. An extended
   timeout or suppressed serial output is not an optimization.
3. **Observe owners with bounded counters:** namei components/provider
   callbacks, mount-table scans, Dentry hits/misses, page faults/COW,
   PTE/usercopy pages, context switches and blocked waits, block requests/
   sectors/flushes. Keep counters opt-in and report them once at guest exit
   or an explicit test marker; avoid logging each syscall on a hot path.
4. **Identify the dominant category before changing an algorithm.** E.g.
   a high mount-scan count justifies an index keyed by owned mountpoint
   Dentry; a high usercopy count requires a page-granularity measurement;
   I/O waits require request/flush evidence. Do not build a generic page
   cache, asynchronous block queue or SMP locks just because Linux has one.
5. **A/B one layer at a time:** identical original application and guest
   parameters, repeat observations, exact output correctness, focused
   host owner/rollback tests, three-ISA source/boundary parity, then the
   affected native QEMU gates on the same commit. Keep the previous
   working build as rollback evidence.

## Architecture sequence after measured hotspots

1. Consolidate namei -> owned Path handoff so normal operations do not
   resolve twice; remove transitional string-based mount selection rather
   than adding a second namespace engine.
2. Finish VFS-independent inode/superblock and explicit mount topology,
   then adapt AF_UNIX pathname sockets and devfs through those contracts.
3. Only when workload evidence asks for it, add file-backed page ownership
   and coherent read/write/mmap/flush; add block request completion and
   DMA/IRQ lifetimes without leaking those into ext4 or the syscall layer.
4. NetDevice and a protocol stack then consume the same bus/driver/DMA,
   wait-queue and object-lifetime foundations. Graphics and SMP remain
   separate later acceptance gates; one-CPU data structures are not
   accidentally advertised as SMP-safe.

**Validation on this local patch:** `check-vfs-provider.sh`,
`check-architecture-boundaries.py`, `check-three-isa-core.py`,
`check-block-foundation.sh`, `check-full-kernel-sources.sh` pass.
These host/static checks do **not** prove latest-head QEMU or complete CI.
