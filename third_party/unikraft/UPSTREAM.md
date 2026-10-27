# Pinned upstream: Unikraft VirtIO ABI and split ring

Upstream repository: https://github.com/unikraft/unikraft
Upstream commit (staging): `359307a2a4cc9c1feb22683d841650902b799e4d`

Verbatim files:
- `include/virtio/virtio_types.h` from
  `drivers/virtio/include/virtio/virtio_types.h`.
- `include/virtio/virtio_ring.h` from
  `drivers/virtio/ring/include/virtio/virtio_ring.h`.
- `COPYING.md` from upstream.

The original SPDX copyright notices are kept. The Unikraft-specific type
header `uk/arch/types.h` and limits header have been replaced by **port**
headers under `ports/unikraft/include`, not edits to upstream source.

The AArch64 VirtIO-MMIO fallback currently uses upstream `vring_init`,
`vring_size`, `struct vring_desc`, `struct vring_avail` and
`struct vring_used` in real DMA requests. This does NOT mean that upstream
Unikraft's blk or transport drivers are imported or booted yet.

## Exact upstream device protocol and full source (2026-10-27)

Added verbatim from the SAME upstream commit:
- `drivers/virtio/blk/include/virtio/virtio_blk.h` -> `include/virtio/virtio_blk.h`
  (Git blob `7f09b3d55b2cc492105beaf73dc99f08b20f9d4f`): block command/status/feature definitions,
  request header and capacity config wire ABI. The live fallback **does**
  include/use this exact header, not local request struct and magic values.
- `drivers/virtio/include/virtio/virtio_ids.h` -> `include/virtio/virtio_ids.h`
  (blob `7fa6166980bca4a61ec666cef40f4fce4112b512`).
- The uncompiled Unikraft `drivers/virtio_blk.c` candidate was
  **removed** after the separately pinned, unchanged U-Boot original
  VirtIO-Blk driver achieved actual AArch64/RV64 hardware persistence.
  Unikraft's *wire ABI and vring layout* headers remain in use and retain
  their original upstream checksums. See
  `docs/virtio-upstream-provenance.md`.
  The queue *algorithm* in `ports/unikraft/split_queue.c` remains a NaSeed
  adapter; it is not the unmodified upstream Unikraft queue algorithm.

`ports/unikraft/include/virtio/virtio_config.h` is a narrow shim ONLY
for the upstream public block-wire header; platform VirtIO operations use
NaSeed's existing transport code, not fake Unikraft config/IRQ providers.
License notices on all imported files are unchanged; file-level notices
must be kept if the full implementation is eventually ported.

## Split queue adapter (2026-10-27)

The *original upstream* vring layout and algorithms in `virtio_ring.h`
remain byte-identical. NaSeed's single `ports/unikraft/split_queue.[ch]`
is a minimal allocation/submit/completion bridge to the existing transport
and DMA ABI. It rejects non-coherent devices for now rather than pretending
to support cache maintenance. Failures poison the queue; close resets the
transport before freeing DMA memory.

The host regression uses the actual upstream layout, tests descriptor
chains, 1024 completion/index wraps, out-of-order completion, malicious
used-ring IDs, and DMA cleanup. Three ISAs compile the same source.
The standalone split-queue host test uses MOCK completions; the physical
AArch64/RV64 QEMU ext4 persistence tests instead link and execute the
**pinned original U-Boot GPL-2.0+ block driver** plus this queue adapter.
