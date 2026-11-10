# M183: one scheduler, boot-composed deferred services, honest removal status

## Scope and ordering

NaSeed still has one ordinary Task dispatcher and one bounded kernel-worker
slice at each user scheduling boundary. The kernel is **not preemptible**;
hardware IRQs only acknowledge their source, set pending state and wake
existing workers. There is no second scheduler or per-driver polling thread.

`kernel/task/scheduler_service.c` now knows only the generic service registry,
kernel-worker timer/fairness accounting, and one bounded bus-removal retry.
`kernel/init/main.c` is the boot composition root: it installs persistent,
statically owned callbacks in a specified order, independent of ISA.

| Boundary | Registered work | Invariant |
| --- | --- | --- |
| Before worker | Async block IRQ completion and M178 physical oracle | Hardware ACK is a prerequisite; no speculative poll |
| Before worker | M179/M180 ext4 worker activation | Runs after block-completion/oracle work |
| Before worker | GPU coalesced presentation and success report | Presentation precedes report; DMA/IRQ ownership stays in driver |
| Before worker | VirtIO input bounded drain and IRQ report | Input max 64, routed devices never speculative-poll |
| Worker | Timer expiration, fairness check, at most one Worker slice | User Tasks cannot be monopolized |
| After worker | PID1 pending exit check | Must await ext4 unmount and durable hardware flush |
| Last | Bus device removal retry | One bounded attempt, not in hard IRQ |

`nas_sched_register_deferred()` has a fixed static capacity, detects duplicate
and invalid registrations, and rejects changes while callbacks are executing.
Callbacks are registered at boot, *not* on each device probe: removal cannot
leave a callback pointing to freed per-device storage, and a subsequently
plugged device uses the same driver-owned readiness check. This is a mechanism
boundary, not an attempt to replace existing upstream drivers.

## Terminal vs transient removal

`NAS_BUS` intentionally retries `-EBUSY`/`-EAGAIN` while a mounted block or
network consumer retains a pin. VirtIO-GPU is different: the primary
`DisplayDevice` currently has no detach/unpin contract. Its attempted
removal now reports `-EOPNOTSUPP` as a **terminal** state; otherwise a GPU
that can never be safely reclaimed would remain in `-EINPROGRESS` forever.

Host regressions cover PRE/POST callback ordering, registration rejection,
capacity, unbound bus lifetime, retryable busy and terminal unsupported
removal, and the original GPU DMA/IRQ protocol fixture.

## Remaining work, explicitly not claimed complete

- Existing U-Boot network and block ports can encounter a failure after
  class-device unregister but before IRQ/vring/DMA teardown is complete.
  Their teardown needs a unified, resumable ownership state machine and
  targeted fault injection; a tiny silent retry fix is not enough.
- lwext4 mount-failure rollback does not fully propagate failed unmount and
  block-device detach. Include this with the same lifecycle failure tests.
- The signed Alpine software pool is not expanded in this kernel-architecture
  batch. Its M174 catalog already tracks 20 ELF candidates; distinguish
  exec-only from functional coverage. Candidate next additions, **subject to
  signed APK availability on all three ISAs**, are `ncdu`, `jdupes`,
  `rdfind`, `joe`, and `mg`. Reuse the single M174 rootfs/CPIO staging path.

## Verification evidence

- `scripts/check-block-foundation.sh`: host bus, lifetime, original U-Boot
  block/network and transport tests.
- `scripts/check-m177-gpu.sh`: GPU protocol/negative cases, independent
  framebuffer handoff, input/IRQ/net boundary checks.
- `scripts/check-m166-input-port.sh`: real Linux VirtIO input host check.
- `scripts/check-three-isa-core.py`: shared-source ownership/ABI wiring.
- Full target cross-build and real three-ISA QEMU M180 evidence are required
  on the exact remote head; a host-only run does not prove guest correctness.
