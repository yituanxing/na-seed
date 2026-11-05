# M200 — device-backed VMA ownership and original Alpine framebuffer mmap

Status: core MM/Display integration implemented in the local working tree;
**host VM, GPU, fork/COW, exception tests PASS; native three-ISA QEMU and
original `fbi` not yet verified**. No release PASS or merge is claimed.
Prerequisite: the independent M199 original-Alpine/OpenRC two-boot desktop
matrix must pass on amd64, riscv64 and arm64. Do not push an M200 change that
cancels or obscures the exact M199 acceptance head.

## Why this boundary, not another application-count batch

M198 already proves a NaSeed compositor, physical VirtIO-GPU/input, signed
Alpine Vim and persistent ext4 in the same native system-QEMU guest. M199
adds original OpenRC service ownership. But an unmodified third-party fbdev
application (for example Alpine `fbi`) still expects `mmap(MAP_SHARED)` on
`/dev/fb0`. Today its `read`/`write`/`ioctl` personality exists, while MM's
file-mapping path only understands VFS inode frames or private snapshots.

The missing architecture is **not** one special-case pixel memcpy. The
current QEMU coherent GPU DMA provider actually obtains its scanout pages
with `nas_frame_alloc`, then holds one PMM reference per DMA page. It is
possible to give each userspace PTE its own additional `nas_frame_get`
reference and reuse the current three-ISA leaf reclamation. It is **not**
safe to do that for arbitrary firmware/MMIO/non-PMM physical pages; these
must fail closed until a separate mapping ownership model exists. A mapped
device also must remain alive after its original fd is closed, and GPU
scanout requires an actual device-facing present operation.

## One ownership model

1. The canonical `nas_anon_regions` ledger remains the one VMA registry. Add
   one typed device-backed kind and a retained mapping owner; do not create a
   second virtual-address tree or a framebuffer-specific MM algorithm.
2. A device mapping owner pins the exact published `DisplayDevice` and its
   relevant DMA/scanout resources independently of the shared OpenFile pin.
   Every successful VMA clone, split and fork holds its own reference; every
   removal, failed transaction, exec and final MM teardown drops it exactly
   once. `nas_display_unregister_primary` must reject either kind of live
   consumer, with no stale or guessed pointer reuse after rebind.
3. The initial device-mapping capability is explicitly limited to the
   **PMM-managed coherent QEMU DMA** provider. The driver retains its own
   allocation reference; each installed user leaf independently acquires one
   `nas_frame_get` that the established fork/munmap/exec/MM teardown path
   may release. A failed leaf publication rolls that precise reference back.
   `DEVICE_SHARED` must be exempt from COW so fork aliases real scanout.
   Other DMA providers, firmware framebuffer and MMIO/IOVA addresses must
   return `-EOPNOTSUPP` rather than flow through PMM APIs. Do not modify all
   three MMU implementations merely on a false assumption that current QEMU
   DMA pages are untracked.
4. The driver exposes a bounded, page-aligned mapping capability through the
   existing display class. Driver-specific DMA layout, coherency, and
   backing lifetime remain behind it. It must advertise PMM-backed coherent
   physical pages, not merely a CPU virtual alias or device IOVA. A firmware
   framebuffer or unsupported backend must reject mapping explicitly; never
   synthesize ordinary frames while claiming direct scanout equivalence.
5. Writable shared mappings retain shared identity across fork, with no
   private COW copy. `mprotect` must honor VMA logical permissions; usercopy
   and ptrace must not bypass the same ownership and access checks.
6. Present/dirty state is owned by the GPU backend and driven through a
   documented deferred scheduler boundary, not a long VirtIO exchange in an
   interrupt handler or a fake successful `msync`. A user write observed via
   a mapping must reach the real QEMU scanout without an application-specific
   executable-name hook.

## Linux fbdev behavior to admit first

- `/dev/fb0` opened with appropriate read/write access; `MAP_SHARED` with
  `PROT_READ` or `PROT_READ|PROT_WRITE` and page-aligned offset.
- Accept only a range whose pages fit the advertised scanout allocation;
  validate overflow, offsets and `MAP_FIXED` collisions before mutation.
- `close(fd)` after successful `mmap` does not unpin the device mapping.
- Partial `munmap` and VMA split retain independent fragment references.
- `fork` inherits the same visible pixels unless `MADV_DONTFORK` is set;
  no PMM refcount or COW ownership is silently fabricated.
- `exec` and exit release mapping pins so the physical backend can safely
  detach once its remaining users are gone.
- `msync` is explicitly `-EOPNOTSUPP` for device VMAs until an actual
  synchronous completion fence is supported; the independently deferred GPU
  present mechanism refreshes mapped writes. Unsupported private, executable
  or noncoherent mappings also return explicit Linux errors.
- Original `fbi` may also exercise Linux virtual-terminal `VT_*`, `KD*`, and
  fbdev mode/cleanup ioctls. Audit its **unchanged** guest syscall trace and
  implement only independently required, correctly owned TTY/FB UAPI paths
  in the same acceptance batch. `mmap` source completion alone is not `fbi`
  success, and missing VT control must not be papered over by patching `fbi`.

## Existing code to move together

| Existing owner | Connected work |
| --- | --- |
| `kernel/mm/anon_regions.[ch]` | typed VMA owner, ref acquisition and split/clone/unmap |
| `kernel/mm/vm.c`, `fault.c`, `mm.c` | fd mapping entry, PMM ref-per-PTE fault policy, fork shared aliases, replacement and protect |
| Existing three-ISA MMU reclaim | retain the proven PMM ref-per-PTE lifecycle unchanged; verify this assumption in tests |
| `kernel/drivers/display/display.h`, `fbdev.c`, `fbdev_chrdev.c` | published mapping capability and VMA pins independent of OFD pins |
| `kernel/drivers/display/virtio_gpu.c` | native backing-page identity and bounded deferred present/flush |
| `kernel/fs/file_io.c`, `io.h` and FileOps adapter | exact `FD_FRAMEBUFFER` mmap admission before regular-file probing; no device names in common VFS/VM |
| `kernel/drivers/tty` and fbdev ioctl adapters, if the original ELF demands them | standard VT/KD and fbdev mode-state semantics, rather than a per-program bypass |

## Acceptance, without weakening M199

1. A Linux-host reference oracle pins expected `mmap` error/flag semantics.
   Host kernel tests inject failed VMA publication, fork, partial unmap,
   failed detach, fd close, mapped-device removal, and MM destruction. The
   baseline anonymous/private/shared file mapping tests remain unchanged.
2. Compile all actual shared kernel translation units for x86-64, RV64 and
   AArch64; run the existing architecture-boundary and display/driver tests.
3. Trace one **unmodified original signed Alpine `fbi`** ELF's mmap, VT/KD,
   fbdev ioctl and shutdown behavior; run it with a verified genuine image
   on each native ISA. No BusyBox alias, host image copy, application patch,
   or fabricated success token counts.
4. Native QMP screenshots must independently find the exact expected pixels;
   guest syscall/FD/VMA witness must prove `MAP_SHARED` and post-close
   lifetime. Separate this framebuffer writer from the OpenRC desktop to
   avoid competing scanout ownership.
5. Repeat the relevant tests after process exit/`exec` and second mapping to
   catch leaked pins and stale GPU DMA access. Mark M200 PASS only after the
   exact-head three-ISA QEMU matrix, not host-only source checks.

This milestone deliberately does **not** implement DRM/KMS, X11/Wayland,
GPU acceleration, physical GPU hot-plug, SMP, or power-loss persistence.
