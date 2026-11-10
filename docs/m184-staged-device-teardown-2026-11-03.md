# M184: ownership-preserving device teardown and real Alpine functional coverage

## Design boundary

This batch continues the existing NaSeed driver architecture. It adds no
second bus, scheduler, homemade VirtIO protocol, or filesystem stack. The
single-core kernel remains nonpreemptible. IRQ handlers acknowledge hardware;
worker context handles deferred disk and network work. Linux, U-Boot,
Unikraft and lwext4 retain their existing code and responsibilities.

The shared lifetime rule is **retire external consumers, stop hardware DMA,
then release owned resources**. An unsuccessful release does not mean the
object is free. The owner remains in its stable storage until a later
retry completes. A future attach or bus probe must not overwrite it.

| Boundary | Failure that previously lost state | M184 retry invariant |
| --- | --- | --- |
| DMA provider | Invalid allocation shape followed by failed release | Keep a non-NULL CPU alias; reject sync/overwriting allocation; retry free |
| Unikraft split ring | Setup/validation failure with failed DMA free; or failed close | Retain the inactive queue's DMA page/transport; reset before release and forbid I/O |
| U-Boot VirtIO-Blk | Unregistered class but failed ring/header/payload/heap release | One ordered resource owner per stage, no duplicate free or fresh attach |
| U-Boot VirtIO-Net | NIC class retired but IRQ/ring/RX/TX/heap not all released | Persist cleanup state; upstream stop once, IRQ first, bus slot reused only when drained |
| Asynchronous VirtIO-Blk | Probe/remove error followed by zeroing singleton owner | Preserve original transport and retry its owned queue/pages before reuse |
| lwext4 bridge | Mount/unmount error after upstream/bridge/RAM cleanup partially completed | Explicit upstream → bridge → RAM stages; original error retained, pending step retried |

The lwext4 bridge is especially sensitive to close errors: upstream
`ext4_umount()` can finish closing the BlockDevice before reporting a final
error. The bridge's reference count differentiates a still-live upstream
mount from a completed close. A finalized VFS mount must never be resurrected
or `ext4_umount()` called twice for the same ownership.

## Real fault-injection evidence

The host fixtures inject repeated DMA release errors, unactivated queue
residues, class removal with live references, stale bus slots, partial
mount cleanup, and final-close flush failures. They assert the resource
remains owned while blocked, exactly-once release after retry, no I/O
against a retired queue, and successful reuse only after cleanup.

The signed Alpine program pool is **not counted as newly expanded**: M174
still has 20 historical signed ELF candidate paths. Its existing single
CPIO/boot-rootfs workflow now also checks the original D-Bus `dbus-uuidgen`
ELF on real guest `/tmp`: `--ensure=FILE` creates the 33-byte machine ID,
`--get=FILE` retrieves it, repeated ensure preserves it, and malformed
existing content fails without destructive replacement. The unique native
marker is `M184_SOFTWARE_DBUS_UUID_FILE_FUNCTION_PASS`.

## Verification requirements

- `sh scripts/check-block-foundation.sh`: common DMA/ring and original U-Boot
  net/blk with fault-injection and device bus contracts.
- `sh scripts/check-m177-gpu.sh`: prior GPU, network, input, IRQ, VFS and
  block ownership behavior unchanged.
- `sh scripts/check-m184-software-functions.sh`: signed-package manifest,
  single-rootfs linkage and static guest-test coverage.
- lwext4 host fixture: failed mount and final-close errors, followed by a
  genuine successful remount on a valid ext4 image.
- **Only real M179/M180 three-ISA QEMU plus physical disk verification** may
  establish full guest acceptance. Host PASS alone is not enough.

## Explicit limitations

- A DMA provider claiming a successful allocation without any CPU alias,
  then also failing to release it, violates today's provider contract.
  Existing high-level `.cpu` ownership checks cannot fully recover that
  case without a separate explicit provider ownership bit.
- VirtIO-GPU and VirtIO-Input still have historical teardown paths that
  discard failed-release status. Their owner/consumer contracts require
  a later focused batch, especially GPU's currently unsupported hot-unplug.
- This batch does not claim additional signed Alpine packages, SMP,
  preemptible kernel execution, or functionality beyond the actual markers.
