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
- `drivers/virtio/blk/virtio_blk.c` -> `drivers/virtio_blk.c`
  (blob `81eb5e284f418c78f5023b17142fc679114f3908`): entire original mature Unikraft C implementation
  is kept **unmodified as a port candidate**, NOT YET BUILT/EXECUTED.
  It requires `ukblkdev`, `uksglist`, `virtqueue`, `ukalloc`, IRQ,
  and Unikraft bus/lcpu APIs. Vendoring it does not constitute porting it.
  The existing polled NaSeed request engine remains the verified fallback.

`ports/unikraft/include/virtio/virtio_config.h` is a narrow shim ONLY
for the upstream public block-wire header; platform VirtIO operations use
NaSeed's existing transport code, not fake Unikraft config/IRQ providers.
License notices on all imported files are unchanged; file-level notices
must be kept if the full implementation is eventually ported.
