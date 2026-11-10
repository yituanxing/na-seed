# M185: GPU/Input teardown ownership and genuine Alpine iostat exercise

## Scope and ownership boundary

M185 finishes the two driver lifetime gaps identified at the end of M184.
It does not introduce another bus model or change kernel preemption. The
single-core kernel stays nonpreemptible, with device interrupts serving as
the wakeup/acknowledgement boundary and work performed outside hard IRQ.

The owner sequence is **withdraw the published consumer, stop IRQ/DMA,
release transport resources, then make static storage reusable**. A cleanup
failure retains its original owner and transport so the same resources can
be retried. A second probe must never overwrite still-owned DMA pages.

| Device | Published consumer / lifecycle | Failure and retry handling |
| --- | --- | --- |
| VirtIO-GPU | The active framebuffer's primary `DisplayDevice` has no unpin/unregister API; its remove path deliberately returns `-EOPNOTSUPP` | Failed probes retain the original GPU transport; IRQ unbind, split-ring closure and all 469 pixel, two entry, command and response DMA pages are scanned on retry, including partially allocated/failed-release pages |
| VirtIO-Input | `nas_input_unregister_device()` must succeed before transport shutdown; pinned evdev readers keep the input class and hardware active | After successful class retirement, IRQ/queues/event DMA are released in order. An error leaves the slot poisoned and reserved for retry; probe rollback retains uncounted owners without pretending that the bus bound a driver |

Neither implementation claims general hot-plug support. The GPU primary
display cannot be detached until the display/fbdev class supports consumer
release. Input can defer removal while readers remain pinned and fully
retire on a later attempt; bus-level retries for errors other than
`-EBUSY`/`-EAGAIN` may still require an explicit caller retry.

## Deterministic host verification

The M177 GPU fixture covers failed IRQ unbind, active and inactive-owned
VirtIO queue teardown, pixel/entry/command/response DMA failures, and
partial DMA allocation rollback with multiple rejected release attempts.
These assert that cleanup keeps its original owner, retries safely and
releases each page exactly once.

The M166/M175 Input fixtures cover an inactive-owned queue after failed
probe, multiple provider release failures, a failed IRQ bind, class
unregister blocked by a reader, and successful detach/reprobe after the
reader is released. Public input stays usable while still registered;
the teardown-pending slot no longer polls or claims IRQ activity.

Required host gates:

- `sh scripts/check-m177-gpu.sh`
- `sh scripts/check-m166-input-port.sh`
- `sh scripts/check-m175-irq-core.sh`
- `sh scripts/check-m178-blk-irq.sh`
- `sh scripts/check-m185-software-functions.sh`
- `python3 scripts/check-three-isa-core.py`

## Genuine Alpine functional addition

The signed Alpine program pool remains **20 original signed ELF candidate
paths, 13 signed APK packages, and 76 overlay catalog entries**. There is
no additional package and no second root filesystem.

The original Alpine `iostat -c 1 2` now runs as a guest functional test:
it reads the kernel's actual `/proc/stat`, prints two `avg-cpu:` samples,
and checks six numeric utilization percentages per sample. The strict
three-ISA native marker is:

`M185_SOFTWARE_IOSTAT_CPU_FUNCTION_PASS`

This supplements, without replacing, the M184 genuine D-Bus machine-id
test and earlier signed Alpine software/IRQ/ext4/persistence gates.

## Acceptance boundary

Host fixtures and static cross-compilation establish deterministic local
regressions only. The batch is accepted end-to-end only when both M179 and
M180 workflows run in QEMU on **x86-64, RV64 and AArch64**, reach the
M185 guest marker, preserve the earlier software/IRQ/GPU/ext4 evidence,
and pass physical ext4 persistence verification with read-only fsck.
