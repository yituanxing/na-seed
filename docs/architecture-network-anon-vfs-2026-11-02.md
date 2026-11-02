# NaSeed architecture integration: network state and unnamed VFS inodes

**Date:** 2026-11-02. **Starting tree:** `naseed-app-batch2@dc86cf2`,
after M151–M155. This note records **new source changes and host gates**, not
an unobserved three-ISA system-QEMU result. The historical 169 signed Alpine
ELFs / 507 executions / 15 guest jobs belong to that **starting** commit.

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

The snapshot is currently **one loopback plus at most one attached NIC**,
not a generalized IP-address service or multi-NIC manager. Link resync is
asynchronous (IRQ wake plus bounded polling), and SMP synchronization is not
present. `kernel/net/ipv4_loopback.c` is still required by the default
non-lwIP build: its complete retirement must include Makefile, the two
full-kernel cross-build scripts, syscall conditionals, host tests and source
manifest checks in a separate all-or-nothing migration.

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
operation. This does **not** make every legacy 256-byte projection a full
PATH_MAX pathname: `nas_vfs_path_string`, diagnostic `origin_path`, cwd
display and parts of metadata/syscall compatibility remain bounded.
Future fixes should migrate those consumers to owned Path references rather
than lengthen a second string-based namei.

## Focused proof (run on source working tree)

| Gate | Scope | Expected claim |
| --- | --- | --- |
| `sh scripts/check-lwip-foundation.sh` | Real upstream lwIP, native worker, Socket FileOps, NETLINK_ROUTE; carrier up/down/up | Host tests only |
| `sh scripts/check-lwip-socket-abi.sh` | Legacy and lwIP Linux socket ABI, DHCP-less NIC flags/MTU/ifindex | Host tests only |
| `sh scripts/check-vfs-provider.sh` | Real VFS mount/lease + anonymous inode lifecycle; memfd ABI | Host tests only |
| `sh scripts/check-path-contract-host.sh` | 4095/4096 length and root namespace error codes | Host tests only |
| `sh scripts/check-m106-usercopy-syscalls.sh` | Real File/FD syscall publication/close, failure rollback, memfd diagnostic bounds | Host tests only |
| `python3 scripts/check-three-isa-core.py` | All 71 shared source units selected in three build manifests | Static manifest, **not** cross-compilation |
| `python3 scripts/check-architecture-boundaries.py` | VFS, MM, driver dependency fences | Static source check |

The local executor for this batch has `cc`, but lacks `clang` and
`qemu-system-{x86_64,riscv64,aarch64}`. The native exact-head CI/QEMU
workloads must therefore be inspected **after push**, before treating the
new source changes as proven in guests or merging the Draft PR.

## Remaining architectural gates, in order

1. Exact-head three-ISA native QEMU Alpine/BusyBox/network/memfd acceptance.
2. Multi-NIC and IRQ/link-loss/remove tests before claiming a general
   network-device lifecycle or hotplug support.
3. Retire the compiled legacy IPv4 profile only after the default boot and
   three-ISA build selection are migrated together; no leftover #ifdef
   behavior fork in the active network syscall layer.
4. Replace remaining 256-byte *active* pathname compatibility paths with
   Path-first ownership. Add a private shmem mount only when an independent
   memfd/no-tmpfs use case requires it.
5. Finish device/Task wait and teardown ownership before adding SMP locks,
   RCU or other Linux/NaOS multiqueue complexity.
