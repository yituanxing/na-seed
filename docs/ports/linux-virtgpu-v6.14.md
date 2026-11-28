# Linux VirtIO-GPU v6.14 port to NaSeed: source inventory and ABI boundary

## Upstream identity — copied, not rewritten

Source: https://github.com/torvalds/linux
Release: `v6.14` (annotated tag object `35ff3b0f4596c82ec0a3e1cc43e2a85f5e976023`)
Peeled upstream commit: `38fec10eb60d687e30c8c6b5420d86e8149f7557`.

The vendor snapshot is at `third_party/linux-virtgpu/`. **All upstream file bytes remain unchanged.**
The original Linux `drivers/gpu/drm/virtio/` directory, relevant DRM/VirtIO UAPI,
and upstream COPYING have their original paths within that snapshot.
`scripts/linux-virtgpu-v6.14.blobs` pins every upstream Git blob SHA,
including copyright notices and original per-file licensing.
`sh scripts/verify-linux-virtgpu-v6.14.sh` requires 100% exact matches and forbids untracked additions inside the vendor tree.
If adaptation needs changes, use separate adapter files or individually reviewed patches.

The upstream files include permissively licensed portions and portions requiring GPL-2.0
compliance (notably DRM/GEM components). Copying a permissively licensed file does not
remove the obligations of a GPL-licensed dependency. **No NaSeed-wide license has been
assumed, and no redistribution or merge conclusion is being made here.**
Review the full effective license obligations before linking or distributing this port.

## Verified boundary — distinct from accelerated rendering

`ports/linux-virtgpu/include/` contains ONLY the minimal integer and asm-generic ioctl
UAPI types needed to compile **unchanged original Linux UAPI declarations**.
`sh scripts/check-linux-virtgpu-uapi.sh` cross-compiles their sizes, offsets,
and ioctl command numbers on x86-64, RV64 and AArch64.

This contract does **not** build `virtgpu_vq.c` or `virtgpu_ioctl.c` as NaSeed
kernel objects. It does **not** create `/dev/dri/renderD128` or enable VirGL
rendering. **The LIVE 2D NaSeed driver `kernel/drivers/display/virtio_gpu.c`
now includes the unmodified upstream Linux `linux/virtio_gpu.h` and uses its
original command IDs, feature bits, pixel format, response types and compile-time
wire layout assertions**, replacing those locally duplicated numeric constants.
The original 2D queue/IRQ/scanout logic is deliberately retained until the
whole upstream Linux DRM/GEM/fence dependency stack can be linked.

The port CI explicitly compiles `NAS_ENABLE_VIRTIO_GPU=1` on x86-64, RV64
and AArch64; the existing QEMU VirGL host capability probe is also required
to catch any live PCI/MMIO protocol regression. Passing this is **not** the
same thing as having linked Linux's complete 3D driver.

## Upstream interfaces to satisfy, in dependency order

| Original Linux dependency | Real NaSeed implementation today | Required port boundary / gap |
|---|---|---|
| `virtio_driver`, `virtio_device`, `virtqueue` & feature negotiation | `kernel/drivers/core/bus.h`, `kernel/drivers/virtio/transport.h`, `ports/unikraft/split_queue.h`, `ports/linux-virtgpu/virtqueue_bridge.[ch]`, `virtqueue_api_port.[ch]` | Built: Linux-named submit/get_buf/kick/notify/callback API on real NaSeed queue; canonical LP64 SG layout and proven DMA pin registry. Missing: mapping arbitrary Linux SG buffer addresses, indirect descriptors, complete `virtio_device` registration and driver/workqueue lifecycle |
| `drm_device`, `drm_driver`, `drm_file`, DRM ioctl dispatch | `kernel/fs/drm_chrdev.c` supports primary node and partial KMS | Linux DRM core, file lifetime, master/render-node semantics not present in equivalent form |
| `drm_gem_shmem_object`, `drm_gem_object`, `dma_resv` | `kernel/fs/drm_gem.c` has per-OpenFile RAM GEM dumb buffers | Existing GEM cannot be passed as Linux shmem BO without implementing reference counts, shared placement, reservations and mmap lifetime |
| `dma_fence`, `dma_fence_chain`, sync_file, `drm_syncobj` | NaSeed has IRQ/fence IDs in its 2D VirtIO command path | Proper asynchronous fence completion and syncobj FD semantics must be provided; do not signal early |
| Linux `work_struct`, `mutex`, `spinlock`, wait queues | NaSeed IRQ + scheduled deferred device services | Adapt process-context completion, cancellation and timeout safely, never sleeping in hard IRQ |
| `virtgpu_ioctl.c` render ioctls & `virtgpu_submit.c` | NaSeed's 2D DRM facade, no full render node | Only publish render node and 3D feature once authentic upstream submit, resource, fence and context APIs work |

## Queue adaptation increment (later than the original UAPI-only baseline)

The real NaSeed split queue now has a deferred submit path and distinct
kick_prepare / notify functions, preserving its original eager-submit API
for all existing clients. It also supports the legacy split-ring
disable_cb / enable_cb race check; never claim EVENT_IDX support.

`ports/linux-virtgpu/virtqueue_bridge.[ch]` maintains per-descriptor-head
opaque request cookies for out-of-order completion, without copying or
reimplementing GPU commands or creating another VirtIO transport.
Both x86-64 Makefile and RV64/AArch64 native source manifests compile-link
this adapter when NAS_ENABLE_VIRTIO_GPU=1.

`scripts/check-linux-virtgpu-vq-bridge.sh` cross-compiles the original
Unikraft split queue **and** the new NaSeed bridge for 3 ISAs. Its host
test executes the real queue code against an emulated transport/DMA
provider; it checks batched kicks, out-of-order cookies, NO_NOTIFY,
lost-callback re-arm, and original eager submission. **This test is a
queue behavior contract, not an accelerated rendering or real-QEMU test.**
The new CI gate must pass before this increment is marked verified.

The adapter also records cookies on post-publication DMA-sync errors. Such
requests remain device-owned even though submission returned an error:
`nas_linux_virtgpu_vq_reap_after_reset` refuses to return those cookies
until the existing split queue has reset the transport and retired ring DMA.
This behavior is covered by an explicit failure-injection test. The port
MUST not synthesize a GPU completion or signal a fence on an error alone.

**The first Linux-shaped queue interface is now available** above it,
but the original GPU source is still not a NaSeed-linked object.
Do not confuse the `virtqueue_*` interface test with an original
`virtgpu_vq.c` unit build or a userspace `EXECBUFFER` acceptance.

## Linux virtqueue API adapter — validated interface, not a native GPU object

`ports/linux-virtgpu/virtqueue_api_port.[ch]` now compiles and links in the
GPU kernel on x86-64, RV64 and AArch64. It supplies the **Linux v6.14
function names and call signatures actually used by the original
`virtgpu_vq.c`**: `virtqueue_add_sgs`, `virtqueue_get_buf`,
`virtqueue_kick_prepare`, `virtqueue_notify`,
`virtqueue_disable_cb`, `virtqueue_enable_cb`. These call the
**existing NaSeed Unikraft split-ring implementation** and maintain the
Linux-visible `vq->num_free` count; they do not implement any separate
GPU rendering commands. No Linux upstream source files were edited.

`ports/linux-virtgpu/include/linux/scatterlist.h` keeps the **exact
Linux v6.14 LP64 `struct scatterlist` field layout**, checked with
compile-time size and offset assertions. A queue-local side table records
the physical-frame DMA pin for each SG during submission. The provider's
mapped **bus address** is validated at submission time; a raw or stale
Linux SG, missing pin, stale bus address, or noncoherent mapping is
rejected before posting descriptors. The short-lived SG registry is
consumed on each `virtqueue_add_sgs` attempt, including failures, to
avoid reusing stale C-stack SG addresses. `SG_CHAIN` and `SG_END`
traversal are tested against the real queue; no general Linux SG
allocator is claimed.

The Linux SG API here is **deliberately incomplete**. The original GPU
driver's calls to `sg_init_one`, `sg_alloc_table`, `sg_set_page`,
DMA map/unmap for arbitrary kmalloc/vmalloc buffers, indirect descriptors
and Linux workqueue callbacks are NOT yet safely implemented. Unlike
Linux's full virtqueue core, this bounded adapter requires pre-mapped,
provider-pinned SG pages. No unpinned Linux virtual pointer gets converted
to a guessed physical address. Linux `virtio_device` registration,
`virtio_find_vqs`, and the rest of DRM/GEM/Fence still need real
porting. In particular **the original 1,340-line `virtgpu_vq.c` remains
unmodified and is not itself compiled/linked into NaSeed**. Do not
advertise a functional DRM render node or Mesa 3D acceleration.

Validation: the `scripts/check-linux-virtgpu-vq-bridge.sh` native host
suite now calls original Linux `virtqueue_*` symbols with SG-list
chains, mapped bus addresses, out/in descriptor direction, NO_NOTIFY
behavior, callback rearm, `num_free`, queue-full errors, and rejected
unmapped/stale DMA. It also cross-compiles the actual port C object for
three ISAs and uses the same object in the real GPU kernel builds.
`virtgpu-upstream-port.yml` and the three-ISA QEMU VirGL host protocol
probe passed on the preceding code head. These are **queue integration
and host capability checks, not Mesa render/readback tests**.

## Real borrowed GEM page DMA mapping (next port boundary)

The real NaSeed DMA provider now exposes optional `map_frame/unmap_frame`
operations for a **borrowed, reference-counted, PMM-managed frame**. An
IOMMU-safe provider returns its **device bus address**, never a guessed
physical address. Only the existing QEMU coherent identity-DMA provider
implements those operations currently. Missing/noncoherent providers are
rejected explicitly; this is NOT a general hardware DMA implementation.

`ports/linux-virtgpu/gem_dma_port.[ch]` reuses the existing NaSeed
`nas_drm_gem_map_offset / nas_drm_gem_mmap_pin / nas_vm_device.page`
contract. A mapped page owns **two references**: the GEM object owner pin
and the DMA provider's independent physical-frame pin. For one-shot
command DMA buffers, wait for USED response and any command fence before
unmapping. For **persistent VirtIO-GPU resource backing**, USED completion
of RESOURCE_ATTACH_BACKING does **not** revoke the host's backing-memory
access: retain the mapping until confirmed RESOURCE_DETACH_BACKING or
RESOURCE_UNREF, with relevant fences, or verified device reset. Failed
unmap preserves both references.

`ports/linux-virtgpu/virtqueue_bridge.c` now accepts mapped-page
scatter/gather slices, converts them through the provider's checked
bus addresses, and publishes them through the real Unikraft queue.
The original 2D GPU path and all existing block/input queue semantics
remain unchanged.

The native GPU build links both adapters on x86-64/RV64/AArch64.
`scripts/check-linux-virtgpu-vq-bridge.sh` now tests real
`dma.c` mapping (including nonidentity device addresses, pin
refcounts, unmap failures, overflow rollback), the GEM pin lifecycle,
and a fully linked host **one-shot GEM command page → DMA bus →
split-ring submission → USED completion → safe release** sequence.
That host test emulates the external transport and GEM lookup only.
**It is not a real QEMU run, upstream Linux driver link, or VirGL 3D
test.** The final-head CI must pass before treating this increment
as verified.

The next missing boundary is the **ordinary Linux SG buffer-to-DMA
mapping** (not the already-tested Linux-named queue API): original
`sg_init_one`, vmalloc/sg_table, atomic-context mapping, indirect
descriptors and callback/workqueue lifetime; then full
`drm_gem_shmem_object`, `dma_resv`, `dma_fence`, syncobj and
render-node semantics. Native upstream `virtgpu_vq.c` unit compilation
and actual Mesa EXECBUFFER must remain explicit acceptance gates.

## Port acceptance sequence (non-negotiable)

1. **Source authenticity / live UAPI bridge**: upstream blob checks, exact UAPI cross-compilation, and live NaSeed 2D GPU build on 3 ISAs — the current step.
2. **Native Linux driver unit linkage**: implement legitimate compatibility interfaces for the *upstream source*, not a parallel rewritten GPU implementation. Fail builds for missing Linux DRM features; never add success stubs.
3. **Real 3-ISA GPU device**: use QEMU's VirGL-capable PCI/MMIO devices and verify command completion plus DMA lifetime under original driver source. Existing 2D regression must stay green.
4. **Real Mesa**: unchanged Alpine Mesa opens real DRM render node, obtains capsets, allocates resources, submits EXECBUFFER, waits for fences, performs GL pixel readback.
5. **Desktop**: Weston with actual virgl guest renderer and frame latency/CPU measurements. Preserve CPU-rendered fallback.

The separate experimental self-written 3D implementation was reverted to its 2D
baseline and its PR #154 closed. This branch is intentionally based on the
pre-experiment 2D head rather than extending the discarded experiment.

Reference adaptation example (not copied into NaSeed): FreeBSD DRM-KMOD PR #517
https://github.com/freebsd/drm-kmod/pull/517
which retains the Linux v6.14 driver source and layers a kernel API shim around it.
The unmerged FreeBSD port reports its VirtIO PCI path; NaSeed must also retain
RV64/AArch64 MMIO operation.
