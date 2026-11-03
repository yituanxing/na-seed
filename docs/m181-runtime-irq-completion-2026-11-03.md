# M181: runtime VirtIO completion without scheduler-frame polling

This is an implementation/acceptance record for the existing single-core,
non-preemptible-kernel NaSeed. It does not introduce a second driver framework
or a new generic request lifecycle.

## Runtime ownership

| Path | Submit owner | IRQ phase | Deferred completion owner | Failure lifetime |
| --- | --- | --- | --- | --- |
| VirtIO-GPU 2D | scheduler-side `nas_display_present` | source ACK and pending bit | GPU's one-in-flight transfer/flush state machine, invoked at a scheduler boundary | failed/timed-out DMA retained until transport reset |
| U-Boot VirtIO-Net TX | lwIP's existing kernel worker via original U-Boot `send()` | NIC source ACK and `tx_completion` semaphore post | original `get_buf()` on its preserved worker C stack after park/wake | failed/timed-out TX poisons and resets device before buffers are reusable |
| VirtIO-Net RX | existing NIC IRQ producer | ACK and existing RX-worker wake | unchanged lwIP RX worker | existing netdev/device pin and hot-remove rules |

VirtIO-GPU's four **bootstrap** protocol commands remain bounded synchronous
because the task dispatcher cannot yet park a kernel stack before the display
has attached. Once the userspace framebuffer is active, neither transfer nor
flush polls a used ring speculatively: each outstanding stage completes on a
corresponding used-buffer IRQ. Dirty writes arriving while an older epoch is
in flight remain queued for a subsequent transfer. Timeout, wrong head, short
response and wrong fence poison the device; they do not free or reuse backing
DMA with a potentially live device reference.

VirtIO-Net's pinned upstream U-Boot code keeps its synchronous `send()` ABI.
On a routed NIC, the lwIP worker parks on a bounded semaphore wait. A routed
NIC invoked outside a worker fails closed rather than falling back to polling.
Only a non-routed early/compatibility client may use the bounded legacy poll.
No upstream U-Boot, lwIP, or scheduler code is duplicated or patched.

The follow-up correctness pass also closes the RX/TX rendezvous race: an RX
worker that encounters an in-flight parked TX records one retry request. TX
release consumes it and wakes RX once, without waiting for another hardware
interrupt. Real-QEMU M176 statistics now require **both** physical NICs to
prove TX worker park and IRQ wake, with zero legacy TX polling, in addition
to the existing RX and captured-wire evidence. The M177 native GPU oracle
accepts at most one fenced command in flight while still checking that five
transfers and flushes completed.

## Checks in this batch

- `sh scripts/check-m177-gpu.sh`: actual GPU protocol and negative cases;
  IRQ-only runtime, dirty epoch, timeout DMA lifetime, display handoff.
- `sh scripts/check-m176-net-irq.sh`: both real upstream Net adapter and
  shared IRQ-source contracts; two NICs, worker park/wake, timeout,
  non-worker fail closed and non-routed legacy compatibility.
- `sh scripts/check-block-foundation.sh`: existing generic block/bus/driver
  DMA ownership and upstream adapters.
- `python3 scripts/check-three-isa-core.py`: the shared x86-64, RV64 and
  AArch64 source/link manifest; this is **not** an ISA runtime test.

All four host checks passed locally on 2026-11-03. The M179 filesystem host
suite could not run locally because `mke2fs` is not installed. Real system-QEMU
GPU/NIC regression on each ISA must still be read from GitHub Actions; host
checks alone do not prove hardware interrupt delivery or userspace behavior.

## Remaining boundary

`scheduler_service.c` still enumerates specific deferred services. The GPU
callback is now bounded, and network TX uses the existing worker wait
primitive. A common deferred-service registry may remove this scheduler
coupling later, but introducing it concurrently would broaden this batch's
ownership risk. The next architectural pass should also verify device-removal
versus pending worker/IRQ lifetimes across all drivers, rather than adding a
new work queue merely for GPU or Net.
