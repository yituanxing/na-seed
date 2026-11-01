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
The first network driver candidate is now **byte-identical U-Boot VirtIO-Net**
at the same `ece349ade2973e220f524ce59e59711cc919263f` revision already
used by the real VirtIO-Blk backend. Original `virtio_net.c` (GPL-2.0+)
and `virtio_net.h` (BSD-3-Clause) are held under `third_party/uboot/`,
hash-checked and cross-syntax-checked using the same tiny U-Boot device-model
compatibility declarations as block. The original network driver is now
**linked** to a real `nas_netdev` class adapter and two common DMA split
queues, tested by a simulated host transport. The source has an unbounded
TX completion wait; `net_port.c` bounds completion polling inside its queue
adapter and converts timeout to poisoned `-EIO` outside upstream.
Keep [Zephyr's VirtIO-Net](https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/ethernet/eth_virtio_net.c)
(Apache-2.0) as a fallback, but it would introduce an otherwise unused
Zephyr net_pkt/devicetree/virtqueue compatibility boundary.
NaOS's GPL-3.0 driver implementations are architectural references only,
given NaSeed's unresolved combined-work licensing. This dependency pin does
**not** mean lwIP is compiled/linked, a Zephyr NIC driver is ported or real
QEMU network traffic is proven.

## Dependency direction (one of each owner)

```text
Linux userspace  socket/connect/send/recv/poll/epoll
       |
kernel/fs/socket_syscall.c      (Linux ABI, sockaddr and usercopy ONLY)
       |
nas_open_file + nas_file_ops    (shared fork/dup/SCM_RIGHTS lifetime)
       |
ports/lwip/socket_port.c        (future: Netconn<->File and readiness)
ports/lwip/netconn_bridge.c     (native-worker boundary for Netconn waits)
       |
official lwIP 2.2.1            (TCP, UDP, IP, ICMP, ARP, DHCP, DNS)
       |
ports/lwip/netif_port.c         (linked/host-tested pbuf<->Ethernet frame)
       |
kernel/drivers/port/net.[ch]   (one Ethernet registry, pinned ownership)
       |
ports/uboot/net_port.c         (original U-Boot driver + thin OS adapter)
       |
nas_bus_driver match type=1    (one driver, both MMIO and PCI)
       |
nas_virtio_transport + DMA     (existing PCI/MMIO queues, IRQ resources)
       |
QEMU VirtIO-Net hardware
```

- The **kernel bus** knows device class and transport, not frames, IP,
  sockets or user descriptors. A discovered but unclaimed network adapter
  remains available for later `nas_bus_driver_register`. The two class
  drivers now have **independent opt-ins**: `NAS_ENABLE_VIRTIO_NET=1` for
  the actual original NIC and `NAS_ENABLE_VIRTIO_BLK=1` for block/ext4.
  Common bus/transport/vring code is included once in either build. The
  native netdev implementation is already part of shared kernel sources;
  the optional VirtIO object list no longer compiles it twice.
- `nas_netdev` is solely an Ethernet frame-facing interface; never a second
  socket implementation, routing table, DHCP client or TCP stream. A
  netif acquires a reference, then detaches before the driver can unregister.
  Send must retain/copy into driver-owned DMA before success; receive is a
  synchronous borrowed buffer copied into lwIP pbufs. `-EAGAIN` is genuine
  backpressure. Its bounded `poll` and IRQ-safe wake callback follow Linux
  NAPI's event/budget distinction; an IRQ only schedules a worker, never
  calls lwIP. A final netif reference cannot drop with a wake callback
  still installed. The NIC driver, not this interface, manages queue and IRQ.
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

## NaOS and Linux cross-check: avoid architectural drift

The exact NaOS kernel comparison baseline is
[`aether-os-studio/na-kernel@2c456491`](https://github.com/aether-os-studio/na-kernel/tree/2c45649144afa3dd2e32147d7029e354dc9fc518).
Its `modules/drivers/net/virtio-net/net.c` publishes a NetDevice;
`modules/net/netserver/lwip_netif.c` adapts that device to official lwIP;
`modules/net/netserver/lwip_socket.c` gives Netconn sockets ordinary
`read/write/poll/release` file operations; `kernel/src/net/net_syscall.c`
dispatches the protocol family. NaSeed already has the *single*
`nas_open_file`/`nas_file_ops` and AF_UNIX backends, so only the AF_INET
provider should change. Do **not** add a second socket descriptor namespace,
duplicate VFS ownership, or copy NaOS's GPL-3.0 adapters.

The NaOS git history is also a caution against equating an imported driver
with a functioning kernel integration: Rust VirtIO
[`09e75a10`](https://github.com/aether-os-studio/na-kernel/commit/09e75a1082afffa3cdfebac27dd7d18da5fe59ee)
was initially untested, was removed for the C/module approach in
[`b6f92e7e`](https://github.com/aether-os-studio/na-kernel/commit/b6f92e7ee2bd1fc935bddcfbca5fd585388b9d91),
the block queue was repaired in
[`f6390dde`](https://github.com/aether-os-studio/na-kernel/commit/f6390dde3f9c991faec4ce54f47d7053ce112d61),
lwIP first arrived with known issues in
[`1e423aa8`](https://github.com/aether-os-studio/na-kernel/commit/1e423aa83262485624b494c57070ce4bae4c870b),
and MMIO followed in
[`3773457b`](https://github.com/aether-os-studio/na-kernel/commit/3773457b1cf4fbfb664f72f7fe7bbd58bdb588be).
We require executable host and then guest evidence for each NaSeed layer.

Linux's [VFS `struct file`](https://docs.kernel.org/filesystems/vfs.html)
and [socket file operations](https://github.com/torvalds/linux/blob/master/net/socket.c)
confirm that `dup`/`fork` share one open description, not protocol-specific
FD tables. Its [NAPI guidance](https://docs.kernel.org/networking/napi.html)
separates IRQ notification from bounded RX polling and IRQ rearming. NaSeed
currently implements the notification/worker budget only: genuine PCI/MMIO
IRQ acknowledge, rearm and guest packets remain unproven.

**Scheduler boundary:** `sys_arch_sem_wait` and `sys_arch_mbox_fetch` can
park only a native `nas_kthread`, while user syscalls execute in a separate
`nas_task` context. Consequently, direct blocking `netconn_*` calls in
`socket_syscall.c` or a FileOps method would be invalid. Keep Netconn calls
in a bounded native worker, publish completions through the existing Task
blocked-continuation and `poll_description` path, and bind commands to the
open-description lifetime with a generation check before a slot is reused.
NaOS enables `LWIP_NETCONN_FULLDUPLEX` and
`LWIP_NETCONN_SEM_PER_THREAD`; NaSeed currently does not, so serialized
access and deferred final release are required until concurrency is proven.

## Scheduling and stack configuration

The native port now configures `NO_SYS=0`, `LWIP_NETCONN=1`,
`LWIP_SOCKET=0`, `LWIP_NETIF_API=1`: official `tcpip_thread` and the
Netconn API compile against the real `ports/lwip/sys_arch.c`. Official lwIP
retains all protocol algorithms. Host integration **executed** the original
`tcpip_thread` on a native NaSeed kernel continuation, verified native
semaphore and mailbox timeout/wake semantics, automatically initialized the
official `lo` loopback interface and **sent/received UDP on 127.0.0.1
through upstream lwIP** (no synthetic AF_INET pipe), emitted a DHCP frame via
`netif_port.c` and delivered an inbound Ethernet frame to official lwIP.
Run `sh scripts/check-lwip-foundation.sh` to repeat the host proof plus
x86-64, RV64 and AArch64 target C syntax gates.

The remaining operating-system constraints are explicit:

1. `sys_mbox_new` rejects requests above the *real* `nas_kmailbox` capacity
   of 8, and `tcpip_mbox`/Netconn queue sizes are configured at 8. This is
   an honest bounded first port, not a throughput claim. Inadequate worker
   capacity or requested stack size fails closed via a kernel assertion.
2. `sys_thread_new` creates a real `nas_kthread_spawn` continuation with
   the existing 8192-byte stack; host-tested scheduler parking, deadlines,
   `sys_now` and upstream `sys_msleep`. Assess live stack high-water before
   promising high traffic; network/tcpip thread cancellation is pending.
3. `sys_arch_protect` currently relies on today's single-core, locally
   IRQ-masked kernel execution and non-preemptible C scheduler. Before
   enabling SMP/preemptible kernel, add the shared irqsave lock; do not
   introduce a second lwIP-specific scheduler or busy-wait mechanism.
4. `nas_lwip_eth_core_attach` / `core_detach` run on the official lwIP core
   thread. The new opt-in `network_boot.c` starts one native RX worker,
   handles an IRQ-safe `nas_netdev_notify_rx` semaphore wake and falls back
   to a **10ms suspended wait** (not a busy spin) when transport IRQ routing
   is absent. It polls at most 8 frames per slice, copies borrowed DMA frame
   bytes into pbufs and uses `tcpip_input`. Host tests verify actual boot
   callback, DHCP TX, RX and event/time wake. Wiring/acknowledging physical
   PCI/MMIO interrupts, real QEMU DHCP, clean worker cancellation and
   Socket/VFS glue remain pending. `lo` is official lwIP-owned.
5. The native `netconn_bridge.c` is now host-tested with **real upstream
   TCP and UDP loopback**, but it is not yet the Linux Socket ABI: one
   bounded command queue and one native worker own all Netconn operations,
   per-OpenFile-slot generation IDs reject stale completions, and final
   release is deferred to the worker. Its copied 512-byte request chunks
   preserve TCP unread tails and explicitly flag UDP truncation. The worker
   is started at lwIP boot, after the core and optional RX workers. An
   AF_INET FileOps provider must still translate Task block/retry, readiness,
   send/recv message flags, and nonblocking connect semantics. In particular,
   `accept` needs a real OpenFile reservation lasting across the asynchronous
   command; a slot merely observed as free is not a lease. No user program
   is claimed to use Netconn until that ownership and ABI work is complete.
6. `nas_lwip_random32` is currently a noncryptographic clock-seeded PRNG;
   a real entropy-backed source is required for defensible TCP ISNs before
   Internet-facing use. Do not claim network security or wire operation.

## Batches and acceptance gates

| Stage | Change | Proof before claiming success |
| --- | --- | --- |
| A — host-proven | MMIO/PCI publish VirtIO-Net class=1; independent netdev; **actual original U-Boot Net** on two DMA queues, shared VirtIO adapter and bus class binding | Host simulated RX/TX, malformed and timeout tests, multi-ISA syntax, existing BlockDevice proof; **no claim of packets on the wire** |
| B — host-proven | Compile original pinned lwIP under `NAS_ENABLE_LWIP=1`, native `sys_arch`, upstream tcpip thread and `lo` netif; Ethernet pbuf adapter sends DHCP and queues inbound RX | Native sem/mbox/deadlines/worker, **real upstream UDP loopback**, DHCP TX, RX, netdev pins and cleanup PASS; three-ISA C syntax PASS. **No guest/wire, TCP socket or AF_INET FileOps proof yet** |
| C — host boot-proven | Original U-Boot VirtIO-Net with dual DMA queues, independent network opt-in, filesystem-independent bus discovery, lwIP netif, native 8-frame RX worker, IRQ-safe wake callback and 10ms fallback | Host simulated boot/DHCP TX/RX, wake, fallback, pins PASS. Next: PCI x86 and MMIO RV64/AArch64 **real** QEMU guest DHCP lease, ARP, ping and UDP to host; MAC/address/link reporting |
| D0 — host bridge-proven | Native Netconn RPC worker, bounded command copies, real upstream UDP/TCP loopback, OpenFile-indexed generation lifetime and deferred release | Host TCP/UDP bind/connect/listen/accept/send/recv, partial TCP/UDP truncation and stale-close tests PASS. **Not wired to user socket syscalls yet** |
| D1 | Replace AF_INET socket backend with Netconn-backed FileOps, preserve AF_UNIX, pin async accept reservations and shared open-description semantics | ncat local/remote, curl, iperf3, DNS, nonblocking connect, poll/epoll, fork/dup, close/accept races on all ISAs |
| E | Remove old simulated AF_INET code and harden shutdown, resource failure, MTU, hot-unplug | Network + block concurrency, packet loss/backpressure, memory lifetime, repeated NIC reset, all-ISA QEMU regression accumulated as requested |

Don't launch the expensive QEMU M141 matrix for Stage A alone. Do not report
wire-level TCP, UDP, DHCP or packet operation until Stage C/D actually links
the upstream implementation and passes guest/hardware tests. Existing block
and filesystem tests remain mandatory at each major acceptance boundary.
