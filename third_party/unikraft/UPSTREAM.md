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
