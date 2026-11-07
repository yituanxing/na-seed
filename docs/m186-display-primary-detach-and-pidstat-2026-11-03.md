# M186: published-display lifetime, recoverable primary detach and Alpine pidstat

## Why this boundary matters

The M185 VirtIO-GPU driver retained its primary display forever because a
live `/dev/fb0` shared OpenFile could still access the GPU's DMA-backed
pixels after an unsafe device removal. M186 closes that class boundary
without adding a second file descriptor reference system or a new display
driver. The kernel stays single-core and nonpreemptible; GPU IRQ handlers
only acknowledge/wake and the worker continues to perform scanout.

## Ordered ownership

1. The stable `/dev/fb0` character-device node is already pinned once per
   **shared OpenFile** by the common VFS open path. `dup`, `fork` and passed
   descriptors share that owner; only final close drops its pin. `O_PATH`
   conservatively holds a pin. Framebuffer userspace mmap is not offered.
2. The framebuffer character adapter provides that one existing pin count
   to `DisplayDevice` via a registered callback. Display class code remains
   independent of the character-device link dependency, preserving the
   existing standalone display tests.
3. `nas_display_unregister_primary(expected_owner)` refuses mismatched
   ownership and returns `-EBUSY` without altering a published display
   whenever any OpenFile still pins it. After userspace lock, a missing
   consumer-count provider also fails closed.
4. Successful unregister atomically withdraws the class backend and its
   byte length. Only then may VirtIO-GPU unbind IRQ, reset/close its VirtIO
   ring and release the pixel, entry, command and response DMA owners.
5. Failed hardware teardown preserves the original bus/device singleton
   for an explicit retry; other device probes must not steal its storage.
6. The class's locked-primary rule still rejects arbitrary late first
   discovery and all in-place replacements. **Only an explicit successful
   detach creates a one-use vacancy** for a validated rebind; firmware
   takeover remains one-shot.

## Verification

- Existing M172 DisplayDevice, devfs node publication, framebuffer FileOps
  and M177/M185 VirtIO-GPU tests must stay green.
- The M186 class/character-device fixture checks registered-node pinning,
  pinned-reader `-EBUSY`, exact-owner detach, callback registration failure
  rollback and a post-lock detach/rebind generation.
- GPU fault injection checks remove while busy, failed IRQ/ring/pixel
  teardown, owner-preserving retries and successful subsequent probe with
  working framebuffer bytes.
- The normal M179/M180 **three-ISA QEMU** gates are mandatory: x86-64,
  RV64 and AArch64 must run original Alpine/network/GPU plus physical
  ext4 guest writes, host-side persistence and read-only `e2fsck`.

## Software capability, not fabricated package growth

The original Alpine `pidstat -u -p SELF 1 2` now reads live `/proc` task
accounting in two real one-second intervals. The guest oracle checks its
selected nonzero PID and numerical CPU percentages, then emits the unique
marker `M186_SOFTWARE_PIDSTAT_SELF_PROC_FUNCTION_PASS`. This extends M184
D-Bus and M185 `iostat` real-function checks. It does **not** claim any
newly packaged ELF: the baseline remains 20 signed original Alpine
candidate ELF paths, 13 signed APKs, 76 overlay catalog entries and one
root filesystem.

## Remaining deliberate limits

This is a safe primary display owner boundary and a working detach/rebind
path, not a full multi-display compositor or physical GPU hotplug stack.
Framebuffer mmap, modesetting and new graphics drivers remain separate
capabilities. Bus errors outside its transient retry set may require an
explicit remove retry by the caller. No new kernel preemption is enabled.
