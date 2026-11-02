# NaSeed architecture integration: network state and unnamed VFS inodes

**Date:** 2026-11-02. **Starting tree:** `naseed-app-batch2@dc86cf2`,
after M151–M155. The network/anonymous-inode batch was published as
`3622b0c` on Draft PR #148. Its exact-head QEMU rerun subsequently passed
all **15/15** M151–M155 x86-64/RV64/AArch64 jobs (169 original signed
Alpine ELFs across three ports, 507 executions). The 56-workflow summary
was 37 successes, 19 conditional skips and zero failures. These are proofs
for the tested behaviors, not universal Linux syscall parity.

## Reference boundary: Linux and NaOS, without copying whole subsystems

- Linux device model: [bus/driver binding](https://docs.kernel.org/driver-api/driver-model/binding.html).
  Linux networking: [NAPI](https://docs.kernel.org/networking/napi.html).
  A device exists independent of whether IPv4 DHCP has completed; carrier
  changes must reach the protocol stack in its own safe execution context.
- NaOS reference: [netdev](https://github.com/yituanxing/na-kernel/blob/main/kernel/src/net/netdev.h)
  and [lwIP netif manager](https://github.com/yituanxing/na-kernel/blob/main/modules/net/netserver/lwip_netif.c).
  NaSeed keeps its smaller Ethernet frame contract and the official lwIP
  implementation; no second registry, packet stack or Linux FD table.
- Linux VFS: [inode / dentry / file](https://docs.kernel.org/filesystems/vfs.html)
  and [namei](https://docs.kernel.org/filesystems/path-lookup.html).
  An unnamed inode still belongs to a real mounted filesystem and must own
  its mount/inode lifetime independently of any pathname or FD alias.

## Network: one authoritative live interface projection

`ports/lwip/network_boot.[ch]` provides
`nas_lwip_guest_if_snapshot()` with interface name, upstream ifindex,
MTU, administrative state, carrier, and only actually assigned IPv4/prefix.
Existing `nas_lwip_guest_ifindex/ifname/ipv4_ifconf` use the same snapshot.
Linux `SIOCGIFNAME/SIOCGIFFLAGS/SIOCGIFMTU` can therefore observe a NIC
before DHCP or while unplugged; `SIOCGIFCONF` and `RTM_GETADDR` still report
only real addresses. A driver-carrier change is observed by the bounded
native RX worker and reconciled on the upstream lwIP `tcpip_thread`, not in
an interrupt or the Linux socket syscall.

The initial snapshot supported loopback plus one NIC. The later lifecycle
convergence (below) expands it to **one loopback plus eight live devices**,
matching the sole `NetDevice` registry. Link resync remains asynchronous;
SMP synchronization and a general address-management service are not yet
implemented. The legacy `kernel/net/ipv4_loopback.c` and its other TCP/IP
simulation units have since been deleted from every default build profile.

## File lifetime: no namespace round-trip for memfd

The new creation transaction is:

1. The Linux syscall adapter validates MFD options and copies the opaque
   0–249-byte name; the shared FD allocator probes capacity before any
   provider side effect.
2. A VFS anonymous-inode factory chooses an **already registered, live,
   writable tmpfs mount** and creates a zero-link regular tmpfs inode.
   This is not a fake mount and not a `/tmp` pathname lookup.
3. VFS binds the exact mount incarnation, takes its standard inode/mount
   lease and transfers that pin to the **existing shared OpenFile**.
   If pinning fails, the provider aborts and reclaims the unpublished inode.
4. Only after success is a process FD published. Fork/dup share the File;
   mmap takes an independent inode pin; final unpin reclaims the orphan.
   `/proc/<pid>/fd/N` receives a bounded Linux-style
   `/memfd:<name> (deleted)` display hint, never a reopenable name.

This removes the earlier `/tmp/.naseed-memfd-*` creation, chmod and unlink
sequence, as well as its name-collision retries. A hidden shmem superblock
is **not** implemented: an active writable tmpfs attachment is still required.
MFD sealing/hugepage/noexec-seal options continue to fail explicitly.

## Pathname contract

The Path-first walker already accepts up to 4095 bytes; one bounded length
helper now protects root-only fast paths too. Oversized slash-only strings
must return `ENAMETOOLONG`, rather than being mistaken for a valid root
operation.

The subsequent **long-cwd batch** adds an explicitly sized
`nas_vfs_path_string_buf` projection from the *same owned Dentry ancestry*.
It preflights all ancestors before writing and uses two linear walks, without
the old 256-Dentry pointer stack or a second namei. The 256-byte function is
a checked wrapper. `chdir`/`fchdir` retain an authoritative long cwd Path
even when optional `cwd[256]` cannot hold it; `getcwd` returns the live
4095-byte spelling and correct `ERANGE` with heap scratch only for long
outputs. `/proc/<pid>/cwd` also supports long paths, with readlink truncation.

**Not yet widened:** diagnostic `origin_path`, mount snapshots and absolute
bootstrap compatibility still use bounded 256-byte projections. They are
not extra namei engines and should not grow into one. The long-cwd batch
has both focused host regressions and earlier 3-ISA native-QEMU evidence;
the current default-lwIP migration still needs its own exact-head guest run.

## Lexical namespace adapter retirement

The obsolete `kernel/path.[ch]`, `nas_fs_resolve_path` and three
test-only canonical/parent string wrappers are now deleted from the source,
both real and manifest build lists, and regression fixtures. The shared
kernel source manifest decreases from **71 to 70** translation units.
`nas_fs_lookup_path_at` and `nas_fs_lookup_parent_at` remain the only generic
namei mechanisms; backend-local `nas_vfs_join_child` is still needed for
ports whose upstream filesystem APIs accept a directory spelling.

`openat`, `access/faccessat` and `newfstatat` retain their exceptional
rootless-boot `/dev` and Limine module compatibility. Instead of copying
the user's name again and asking a lexical adapter to canonicalize it,
they obtain a checked, bounded, literal endpoint candidate from the
**same owned Path request**. A missing ordinary VFS inode never becomes
a synthetic device, and literal `.`/`..` or multiple separators cannot
escape this whitelist. Production `access` and `newfstatat` preserve their
original mount/dirfd owners while deciding this fallback, without a second
usercopy or a second namei. The future devfs should eventually replace
this small explicit bootstrap exception.

The host oracles now test the actual negative-parent Path operations,
instead of retaining unused canonical wrappers solely to keep tests green.
The M106 fixture directly exercises absolute, rooted-relative, dot/dotdot,
double slash and overlong bootstrap names. Its predecessor exact-head native
three-ISA QEMU job and 56-workflow CI checks passed after publication.

## Default network provider convergence (single upstream lwIP stack)

The obsolete simulated AF_INET TCP loopback backend has been retired from
the repository, its host-only test, x86 linker, shared source manifest,
ARM64/RV64 builders and all syscall/Task/poll branches. Official upstream
lwIP (tcpip thread, Netconn, native FileOps) is the **only** AF_INET protocol
provider in the default kernel on all three ISAs. The kernel's AF_UNIX
endpoint and AF_NETLINK route File ownership remain independent of the IP
protocol stack. In particular, Netconn's asynchronous close keeps its
OpenFile generation lease; a new socket cannot race a closing bridge slot.

Ethernet hardware is still optional: without a discovered NIC, the real
upstream lwIP loopback starts and the guest can use TCP/UDP on 127.0.0.1.
Enabling an U-Boot VirtIO NIC attaches to **that same stack** and uses its
existing carrier/DHCP callbacks. No second loopback packet algorithm or
device table is linked for the default/blk/net/both build profiles.
`NAS_ENABLE_LWIP=0` is now a configuration error, not an implicit switch to
the removed simulation. Source manifest and dry-run guards reject regressions.

This batch's host checks include the real syscall ABI fixture, native
upstream loopback/Netconn integration and all-source selection audit. The
subsequent default-provider exact-head three-ISA QEMU gate passed on
`0470082` (M155, 3/3) before the multi-NIC changes described next.

## Device lifetime and multi-NIC convergence (current batch)

Linux's [netdevice lifecycle](https://docs.kernel.org/networking/netdevices.html)
and [driver binding model](https://docs.kernel.org/driver-api/driver-model/binding.html)
set the order: fully initialize before publishing; stop new consumers and
traffic before removing; do not reclaim queue/DMA storage while another
component retains a reference. NaSeed implements the smaller single-core
version without importing Linux's rtnl/RCU machinery:

1. The **only** hardware authority is `kernel/drivers/port/net.c`, with at
   most eight uniquely named Ethernet devices. Driver registration signals
   one change-notification subscriber; the lwIP manager owns no second
   hardware registry. U-Boot's original VirtIO class currently has **two**
   physical driver slots (`eth0`, `eth1`) on PCI or MMIO.
2. One network worker discovers registered devices by a **short-lived
   registry pin**, and owns bounded RX polling for every attached NIC.
   All calls that add/remove a `netif`, change its link, start/stop DHCP or
   choose a default route run on the **genuine upstream tcpip_thread**.
3. The manager creates one adapter and DHCP lease/status projection per
   physical NIC, in addition to native loopback. It publishes `lo` and at
   most eight physically attached NICs to ioctl and NETLINK_ROUTE. Address
   enumeration includes only actually assigned, carrier-up IPv4 leases;
   the NETLINK multipart queue is sized for nine addresses plus DONE.
4. `nas_netdev_begin_remove()` marks a NIC unavailable to **new pins,
   TX and RX** and wakes the manager. The core detaches that NIC's netif,
   shuts down DHCP, removes its IRQ wake and drops the lifetime pin.
   `nas_uboot_net_detach()`/ordinary synchronous bus unbind returns
   `-EBUSY` until the final pin is gone. DMA is never freed on an unsuccessful
   first unbind; the new bus-owned deferred removal below manages retries.
5. The default route remains on the first live carrier-up interface;
   attaching a later NIC cannot steal it. If that interface disappears,
   the core selects the next attached, usable NIC. One NIC's RX or DHCP
   failure cannot wipe a sibling interface's state.

Proof is layered: actual upstream lwIP host tests cover two NICs, late
registration, independent link/DHCP and RX, two-phase removal, re-register
of the same device storage and route failover. The U-Boot driver host test
checks that a busy unbind leaves DMA alive but TX disabled, then the generic
bus reaper completes after its final pin drains. NETLINK_ROUTE tests an exact full-size
eight-NIC multipart dump. Three per-ISA QEMU wire gates have been upgraded
to attach **two genuine VirtIO PCI/MMIO NICs**, each with a distinct slirp
DHCP subnet (10.0.2/24, 10.0.3/24), while an untouched signed Alpine Ncat
ELF verifies actual guest-to-host TCP through the default interface.

The subsequent **bus removal convergence** adds a single bounded, deferred
retirement mechanism to the existing `nas_bus_device` registry (64 stable
discoverer-owned slots). `nas_bus_device_request_remove()` synchronously
begins driver quiescence, records an idempotent pending request on `-EBUSY`,
and returns acceptance without requiring a caller-driven retry loop.
`nas_sched_service_kernel()` performs at most **one retry per normal Task
scheduling boundary** through `nas_bus_service_removals(1)`. It runs after
the native worker slice, so lwIP can first detach on `tcpip_thread`; no
fifth kernel worker, IRQ-time DMA cleanup or device-specific bus registry is
created. Finished removals unbind **and** unregister; the retired device is
never rediscovered by a bus rescan. Terminal driver errors remain
observable via `nas_bus_device_remove_status()` and require an explicit
new request. The original synchronous unbind API remains intact.

This mechanism also applies to the existing original U-Boot BlockDevice
driver: the first removal marks the BlockDevice as retiring and rejects
**new** mount pins. An already-mounted volume that still holds a block
reference remains fully readable/writable/flushable while the bus removal
is pending; the final unmount allows the same generic deferred workerless
bus service to release its transport. Unlike an Ethernet netif, a mounted
block device must never lose its live I/O capability during the drain.

This is **not** arbitrary PCI hotplug, SMP-safe concurrent device registry
mutation, full Linux routing policy, or IPv6. Platform enumeration remains
bootstrap-time; dynamic core attach/remove is triggered by the registry
events that drivers explicitly issue, and explicit bus removal requests.

## Focused proof (run on source working tree)

| Gate | Scope | Expected claim |
| --- | --- | --- |
| `sh scripts/check-lwip-foundation.sh` | Real upstream lwIP, two native NICs, re-registration, route failover, eight-NIC NETLINK_ROUTE, Socket FileOps | Host tests only |
| `sh scripts/check-block-foundation.sh` | Original U-Boot VirtIO-Net driver unbind/drain/DMA lifetime, async PCI/MMIO bus retire, bounded fair retries, mounted BlockDevice I/O | Host tests only |
| `sh scripts/probe-lwip-virtio-wire-smoke.sh` / `sh scripts/probe-lwip-virtio-wire-multiarch.sh` | Two real PCI/MMIO NICs per ISA, two actual DHCP subnets and signed Alpine TCP | Requires 3-ISA QEMU CI |
| `sh scripts/check-lwip-socket-abi.sh` | Legacy and lwIP Linux socket ABI, DHCP-less NIC flags/MTU/ifindex | Host tests only |
| `sh scripts/check-vfs-provider.sh` | Real VFS mount/lease + anonymous inode lifecycle; memfd ABI | Host tests only |
| `sh scripts/check-path-contract-host.sh` | 4095/4096 length and root namespace error codes | Host tests only |
| `sh scripts/check-m106-usercopy-syscalls.sh` | Real File/FD syscall publication/close, failure rollback, memfd diagnostic bounds | Host tests only |
| `sh scripts/check-path-contract-host.sh` (extended) | Real deep provider: 4095-byte output, >256 ancestors, actual chdir/fchdir/getcwd, ERANGE and owned File Path | Host tests only |
| `scripts/test-procfs.c` (extended) | Long `/proc/<pid>/cwd` full and truncated readlink outputs | Host tests only |
| `python3 scripts/check-three-isa-core.py` | All 69 shared source units selected in three build manifests | Static manifest, **not** cross-compilation |
| `python3 scripts/check-architecture-boundaries.py` | VFS, MM, driver dependency fences | Static source check |

The local executor has `cc`, but lacks `clang` and
`qemu-system-{x86_64,riscv64,aarch64}`. Only **new exact-head** CI/QEMU
results prove the current two-NIC wire behavior, rather than the earlier
single-NIC guest acceptance.

## Remaining architectural gates, in order

1. Exact-head three-ISA dual-VirtIO DHCP/wire acceptance **for this batch**,
   plus normal full guest Alpine/BusyBox/memfd regression.
2. True hardware hotplug notifications and device-tree/PCI enumeration
   refresh; deferred bus-unbind completion now exists but physical hotplug
   detection and SMP-safe registry mutation are deliberately not claimed.
3. Preserve official upstream lwIP as the sole IPv4 provider in every
   default and NIC-enabled native build; reject legacy providers.
4. Replace remaining 256-byte *active* pathname compatibility paths with
   Path-first ownership. Add a private shmem mount only when an independent
   memfd/no-tmpfs use case requires it.
5. Finish device/Task wait and teardown ownership before adding SMP locks,
   RCU or other Linux/NaOS multiqueue complexity.
