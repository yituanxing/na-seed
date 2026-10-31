# Network stack import: NaOS study, NaSeed ownership and rollout

## Decision and provenance (2026-10-31)

**Use original lwIP, not a NaSeed-written TCP/IP stack.** Study NaOS's layering,
but import neither NaOS's GPL-3.0 kernel adapter nor its scheduler/VFS glue.
NaOS's userspace repo is `yituanxing/naos`; the relevant kernel is
[`yituanxing/na-kernel`](https://github.com/yituanxing/na-kernel):

| NaOS layer | Inspected source | NaSeed integration decision |
| --- | --- | --- |
| Linux-compatible socket | `modules/net/netserver/lwip_socket.c` | Keep one NaSeed `nas_open_file`/Linux syscall adapter; use lwIP Netconn handles. Do not import NaOS's VFS model. |
| Protocol and DHCP | `modules/net/netserver/lwip_netif.c`, `lwipopts.h` | Vendor **official lwIP** and provide a small `netif` adapter; no local TCP congestion, ARP, IP or DHCP implementation. |
| OS primitives | `modules/net/netserver/sys_arch.c` | Port `sys_arch` to NaSeed kernel threads, semaphores, mailboxes and monotonic clock; do not copy NaOS process/scheduler types. |
| Ethernet device | `kernel/src/net/netdev.c`, `netdev.h` | Use ONE tiny NaSeed Ethernet-device port backed by upstream NIC drivers, independent of protocol and VFS. |
| NICs | `modules/drivers/net/virtio-net/net.c`, `e1000/e1000.c` | First VirtIO-Net on shared MMIO + modern PCI transports; select an upstream permissively licensed driver before porting. E1000 only if a supported hardware target requires it. |

Selected upstream source **pinned as a Git submodule** at
`third_party/lwip` (fetch with `git submodule update --init`): official
[`lwip-tcpip/lwip` `STABLE-2_2_1_RELEASE`](https://github.com/lwip-tcpip/lwip/releases/tag/STABLE-2_2_1_RELEASE),
commit `77dcd25a72509eb83f72b033d219b1d40cd8eb95`, BSD-style `COPYING`.
For the VirtIO-Net device implementation, evaluate and pin
[`zephyrproject-rtos/zephyr/drivers/ethernet/eth_virtio_net.c`](https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/ethernet/eth_virtio_net.c)
(Apache-2.0). Its VirtIO queue, net_pkt, devicetree and threading dependencies
need a coherent port; do **not** copy a single C file and claim integration.
NaOS's GPL-3.0 driver implementations are architectural references only,
given NaSeed's unresolved combined-work licensing. This dependency pin does
**not** mean lwIP is compiled/linked, nor that a Zephyr NIC driver is ported.

## Dependency direction (one of each owner)

```text
Linux userspace  socket/connect/send/recv/poll/epoll
       |
kernel/fs/socket_syscall.c      (Linux ABI, sockaddr and usercopy ONLY)
       |
nas_open_file + nas_file_ops    (shared fork/dup/SCM_RIGHTS lifetime)
       |
ports/lwip/socket_port.c        (future: Netconn<->File and readiness)
       |
official lwIP 2.2.1            (TCP, UDP, IP, ICMP, ARP, DHCP, DNS)
       |
ports/lwip/netif_port.c         (future: pbuf<->Ethernet frame, link state)
       |
kernel/drivers/port/net.[ch]   (one Ethernet registry, pinned ownership)
       |
ports/<upstream>/virtio_net.c  (future: original driver + thin OS adapter)
       |
nas_bus_driver match type=1    (one driver, both MMIO and PCI)
       |
nas_virtio_transport + DMA     (existing PCI/MMIO queues, IRQ resources)
       |
QEMU VirtIO-Net hardware
```

- The **kernel bus** knows device class and transport, not frames, IP,
  sockets or user descriptors. A discovered but unclaimed network adapter
  remains available for later `nas_bus_driver_register`. The platform's
  VirtIO type=1 acceptance is the first wiring change in this batch.
- `nas_netdev` is solely an Ethernet frame-facing interface; never a second
  socket implementation, routing table, DHCP client or TCP stream. A
  netif acquires a reference, then detaches before the driver can unregister.
  Send must retain/copy into driver-owned DMA before success; receive is a
  synchronous borrowed buffer copied into lwIP pbufs. `-EAGAIN` is genuine
  backpressure. The NIC driver, not this interface, manages queue and IRQ.
- `lwIP` owns the *protocol* and the 127.0.0.1 loopback netif. The existing
  `kernel/net/ipv4_loopback.c` is a temporary **simulated stream** backend;
  after real Netconn and socket acceptance, replace it and retire its
  pipe-rings, not leave two AF_INET implementations permanently.
- `AF_UNIX` and its VFS Dentry/Path integration remain unchanged. An
  `AF_INET` socket still occupies exactly one shared `nas_open_file`; dup,
  fork, close, poll and epoll must observe that identity. No lwIP-private
  Linux FD array or direct user pointer in the stack.
- No device-specific DMA or page-table arithmetic in lwIP. No TCP state
  inside the NIC. RX completion must not call blocking lwIP/Netconn code
  from an interrupt; it wakes a bounded network worker which invokes
  `tcpip_input` (or another thread-safe lwIP netif API).

## Scheduling and stack configuration

Port `NO_SYS=0`, `LWIP_NETCONN=1`, `LWIP_SOCKET=0`,
`LWIP_NETIF_API=1` (as in NaOS's netserver): the `tcpip_thread` and
Netconn API run atop a real `sys_arch.c`. The standard lwIP BSD source
provides all wire protocol algorithms. Implement and test these OS hooks:

1. `sys_sem_*` and `sys_mbox_*`: correct timeout and wakeup semantics using
   `nas_ksem`, `nas_kmailbox`/wait queues, IRQ-masked atomicity and bounded
   storage; `sys_mbox` capacity must be explicitly sized and allocation
   failures propagated. Never return fake success under pressure.
2. `sys_thread_new`: map to genuine `nas_kthread_spawn` with sufficient C
   stack and lifecycle, not a fake inline call. Provide `sys_now` from the
   monotonic clock and per-thread timeouts where configured.
3. Protect lwIP core ownership and scheduling against reentrancy. The
   existing single-CPU kthread primitives are a foundation, **not yet a
   completed lwIP sys_arch**. Future SMP requires a real common lock.
4. `netif` startup: register loopback first; attach an Ethernet `nas_netdev`
   on link-up; let official lwIP handle DHCP and DNS. QEMU user-mode NAT
   DHCP/addressing must be tested on guest, not assumed from host.

## Batches and acceptance gates

| Stage | Change | Proof before claiming success |
| --- | --- | --- |
| A — this batch | Make MMIO/PCI discovery publish VirtIO-Net class=1 alongside block class=2; add an independent Ethernet port/registry and host tests for binding/ownership | Multi-ISA source gates, host tests, existing BlockDevice proof; **no claim of packets on the wire** |
| B | Link the byte-identical pinned lwIP submodule; build `sys_arch`, bootstrap tcpip thread and loopback netif | Host + all three ISA compile; lwIP timer/thread/mbox tests, real loopback TCP/UDP/ICMP without previous synthetic pipe backend |
| C | Pin and port a coherent existing VirtIO-Net driver using the shared transport/DMA; connect netif, interrupt/bounded receive worker | PCI x86, MMIO RV64/AArch64 QEMU guest frames; DHCP lease, ARP, ping and UDP to host; MAC/address/link reporting |
| D | Replace AF_INET socket backend with Netconn and FileOps, preserve AF_UNIX and shared open-description semantics | ncat local/remote, curl, iperf3, DNS, nonblocking connect, poll/epoll, fork/dup, close/accept races on all ISAs |
| E | Remove old simulated AF_INET code and harden shutdown, resource failure, MTU, hot-unplug | Network + block concurrency, packet loss/backpressure, memory lifetime, repeated NIC reset, all-ISA QEMU regression accumulated as requested |

Don't launch the expensive QEMU M141 matrix for Stage A alone. Do not report
wire-level TCP, UDP, DHCP or packet operation until Stage C/D actually links
the upstream implementation and passes guest/hardware tests. Existing block
and filesystem tests remain mandatory at each major acceptance boundary.
