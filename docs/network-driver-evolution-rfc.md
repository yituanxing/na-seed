# Network driver evolution: NaOS Git archaeology, Linux design and NaSeed decision

Date: 2026-10-31. Scope: **network architecture and driver selection**;
this is not evidence that packets work on NaSeed yet.

## What the NaOS Git record actually shows

Kernel examined: [yituanxing/na-kernel](https://github.com/yituanxing/na-kernel),
not the `yituanxing/naos` userspace repository. Commit links below refer to
that kernel and were read alongside current network device, VirtIO-Net,
E1000, `lwip_netif.c`, `sys_arch.c` and `lwipopts.h` sources.

| When | NaOS change | What NaSeed should learn |
| --- | --- | --- |
| 2025-06 to 08 | Earlier VirtIO-Net appeared, was deleted, then C VirtIO-Net returned in [8a5855f](https://github.com/yituanxing/na-kernel/commit/8a5855fab3478354f2b90332c984e53d014dbdcc); [1f9a670](https://github.com/yituanxing/na-kernel/commit/1f9a670eaec710d038e572f639b2031e6cc261d9) added TX completion detection via an indefinite loop | Bind a stable transport first; every TX completion needs bounded progress/failure and DMA ownership accounting. |
| 2025-08-26 | [4b71855](https://github.com/yituanxing/na-kernel/commit/4b71855a8cf34f16c0f0c59bef6389563efa19dd) introduced `netdev` **after** VirtIO-Net and also experimented with `smoltcp` | Keep one protocol-independent Ethernet boundary so stack and NIC can evolve independently; don't grow a second packet stack inside it. |
| 2025-08-28 | [3fda2e4](https://github.com/yituanxing/na-kernel/commit/3fda2e4c5e5573d53e57f7e2debf35eae19abd35) fixed E1000 receiving the completion status only after it had been cleared | Complete/consume/rearm order is part of correctness, not an optional driver optimization. |
| 2025-11 | [c2f244b](https://github.com/yituanxing/na-kernel/commit/c2f244be07f8d6299acb8cbea4301e6f21a4fb9d) introduced an MTU-sized chunking loop in `netdev_send` | Do not reproduce byte-stream style chunking of an Ethernet frame at the device boundary. An oversized L2 frame requires a protocol/offload decision or a failed send. |
| 2026-03 | [753b2fe](https://github.com/yituanxing/na-kernel/commit/753b2fe31db9256fdac4a1e2ef94a78ceca73e60) updated lwIP; [90d4a67](https://github.com/yituanxing/na-kernel/commit/90d4a67d46ad2c55f7aa66a8d075e04c361f4cb4) fixed VirtIO-Net RX buffer handling | Keep original lwIP versioned separately, make the RX ring ownership invariant explicit and never free/reuse DMA in flight. |
| 2026-08 | [9af4fcc](https://github.com/yituanxing/na-kernel/commit/9af4fccfb00feb9de7f88a3bcb61aa2c7c249555) added VirtIO-Net/netserver extensions (multiqueue, GSO, batching) | Add capabilities only after a stable single RX/TX queue pair; do not make every driver implement sophisticated offloads just to attach. |

Current NaOS has a rich `netdev` carrying wireless scan/connection state,
offloads, network configuration, link events and queues. That is useful as a
functional reference, but most fields are **not** intrinsic to a basic NIC
transport. Its `sys_arch.c` and socket/VFS glue are tied to NaOS task and
file models and are not suitable direct drop-ins. NaOS GPL-3.0 driver code
is *not* copied into NaSeed.

## Linux alignment (not a Linux clone)

- [Linux Driver Binding](https://docs.kernel.org/driver-api/driver-model/binding.html):
  `bus_type` owns device/driver matching and late probes. NaSeed already has
  this in `kernel/drivers/core/bus.[ch]`; PCI and MMIO should only enumerate.
- [Linux Network Devices](https://docs.kernel.org/networking/netdevices.html):
  the network-device object is published after a hardware driver's probe,
  and removal waits for users/packet work to stop. NaSeed's `nas_netdev`
  keeps one reference-pinned Ethernet identity, not a second bus model.
- [Linux NAPI](https://docs.kernel.org/networking/napi.html):
  IRQ **schedules** work; an independent bounded poll handles TX completions
  and up to a budget of RX packets. The recent `nas_netdev_notify_rx` wake
  callback + `nas_netdev_poll` preserve that division without new interrupt
  threads in each driver. The eventual lwIP worker must be the caller.
- [Linux Sockets](https://docs.kernel.org/networking/kapi.html):
  sockets retain normal `struct file`/poll ownership. NaSeed keeps one
  `nas_open_file` identity and maps AF_INET to lwIP Netconn; AF_UNIX remains
  a separate transport. lwIP must never keep a parallel user FD table.

## Original-driver selection

| Source | License / integration cost | Decision |
| --- | --- | --- |
| [U-Boot VirtIO-Net](https://github.com/u-boot/u-boot/blob/ece349ade2973e220f524ce59e59711cc919263f/drivers/virtio/virtio_net.c) | GPL-2.0+, about 230 C lines; uses U-Boot `eth_ops` and the same VirtIO API already adapted for original U-Boot VirtIO-Blk | **First port.** Original source and protocol header now pinned byte-for-byte. Generalize existing minimal U-Boot device-model types; one implementation per common queue operation. |
| [Zephyr VirtIO-Net](https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/ethernet/eth_virtio_net.c) | Apache-2.0, around 250 lines; needs Zephyr devicetree, `net_pkt`, `virtqueue` and synchronization adapters | Fallback if U-Boot's completion model proves unsafe to adapt. Do not bring two NIC drivers online for the same QEMU device. |
| [Linux virtio_net](https://github.com/torvalds/linux/blob/master/drivers/net/virtio_net.c) | GPL-2.0-or-later, thousands of lines coupled to `skb`, NAPI, Linux DMA and netdevice helpers | Architecture reference, not a small first transplant. |
| NaOS VirtIO-Net / E1000 | GPL-3.0 and NaOS-specific task/IRQ/netdev interfaces | Historical design and failure cases only; **not importable unchanged** under the current license policy. |

### Important original U-Boot limitations

The pinned `virtio_net.c` is now **linked unchanged** through
`ports/uboot/net_port.c` and host-tested with simulated DMA completions.
It assumes synchronous
TX completion with an unbounded loop, fixed 32 RX buffers, a single pair of
queues, and link up when `VIRTIO_NET_F_STATUS` is not negotiated. Copying it
into a live kernel unchanged without an error-aware transport adapter would
be a regression, not an improvement. The original C/header must remain
byte-identical; a NaSeed port outside `third_party/` must enforce a finite
completion budget, poisoned queue/reset on failure, DMA bounce lifetime,
short/malformed RX rejection, and correct ring rearm. Report `-EAGAIN`,
`-EIO`, or `-ENETDOWN` rather than manufacturing success.

`ports/uboot/virtio_compat.c` now owns the **sole** shared U-Boot
`virtqueue_*`, feature, config and queue-discovery functions. Each original
class driver supplies only its DMA staging/completion bridge to the same
`nas_vring_queue` implementation. Original VirtIO-Blk host regression is green.

## Acceptance and order

1. This audit batch: one upstream source pin, original network-driver source
   syntax/ABI proof, generic U-Boot class-type declarations, and IRQ-wake /
   budgeted netdev host tests. **No packet or linked-driver claim.**
2. **Host-proven:** one shared compatibility layer, a real pinned U-Boot
   VirtIO-Net `eth_ops` wrapper with two DMA queues, fixed 32 RX buffers,
   finite TX polling and malformed completion fail-closed; register a
   type=1 class driver on both MMIO and PCI. The host tests simulate device
   completions; hardware PCI/MMIO IRQ acknowledgment and physical QEMU
   traffic are **not yet proven**. `NAS_ENABLE_VIRTIO_NET=1` independently
   enables the NIC, with no lwext4/BlockDevice requirement. The kernel
   bootstrap now schedules official lwIP and a native bounded RX worker;
   both paths have host-side simulated DMA acceptance.
3. **Host-proven:** original pinned lwIP `sys_arch` and native Netconn-core
   thread, real upstream UDP over its own loopback, DHCP transmit and
   `nas_netdev` Ethernet pbuf adapter. The remaining work is Netconn into
   the one `nas_file_ops` open-file lifetime, guest TCP/IP and IRQ delivery.
4. Accumulate host/ISA gates, then one all-ISA QEMU batch proving DHCP,
   ping, UDP/TCP, DNS and real Alpine network applications. Retire the old
   simulated AF_INET backend only when this replacement passes.

The release-time combined-work licensing decision remains open as documented
in `docs/distribution-license-review.md`.
