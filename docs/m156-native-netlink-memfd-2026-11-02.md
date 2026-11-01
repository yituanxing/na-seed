# M156 — Native NETLINK_ROUTE, UDP first-send and anonymous memfd compatibility

This changes the **shared** x86-64, RV64 and AArch64 NaSeed core in response
to failures from original signed Alpine programs. It does not replace or
recompile the Alpine executables, and does not treat a `--help` invocation as
evidence that a network or filesystem operation worked.

## Actual upstream failures observed before the change

| Cohort | Native QEMU evidence | Real interface deficiency |
|---|---|---|
| M152 | dnsmasq: `cannot create netlink socket: Address family not supported by protocol` | Linux `AF_NETLINK/SOCK_RAW/NETLINK_ROUTE` absent |
| M152 | drill: `Error creating socket` after UDP query | Native UDP `PORT_OPEN` can race immediate first `sendto` |
| M155 | original `xfs_repair` faults in Phase 1 | `memfd_create`/`O_TMPFILE` unavailable, hitting an upstream `mkostemp` fallback that writes a string literal |

The M155 cause was reproduced independently with the **same Alpine binary
and 320-MiB XFS image** on Linux, injecting `ENOSYS` for `memfd_create` and
`EOPNOTSUPP` for the `/tmp` `O_TMPFILE` fallback: the same SIGSEGV occurs.
Injecting **only** `memfd_create=ENOSYS` still completes all seven XFS phases;
the interaction matters. This is evidence of the failure mechanism, not a
claim that the new NaSeed guest has passed yet.

## Shared kernel interfaces

- `kernel/fs/netlink_route.c`: one kernel-owned OpenFile backend for Linux
  `NETLINK_ROUTE` address dumps. It allocates real per-socket port IDs,
  answers `RTM_GETADDR` with `RTM_NEWADDR` and `NLMSG_DONE` using the
  native lwIP interface snapshot, preserves Netlink seq/port identifiers,
  implements `recvmsg(MSG_PEEK|MSG_TRUNC)`, poll and close. An unimplemented
  multicast subscription returns `EPERM` so dnsmasq can take its own
  documented fallback; it is **not** acknowledged as a functioning
  multicast notification stream. IPv6 returns an empty DONE because the
  native NIC currently has no verified IPv6 address.
- `ports/lwip/network_boot.c`: captures the **actual DHCP subnet prefix and
  netif MTU** with the DHCP IPv4 address. The Linux `lo` interface has
  127.0.0.1/8. `SIOCGIFCONF`, `SIOCGIFNAME`, `SIOCGIFFLAGS` and `SIOCGIFMTU`
  all use the same coherent interface snapshot.
- `ports/lwip/socket_port.c`: one copied UDP first datagram can queue behind
  an asynchronous native PCB `PORT_OPEN`, before the real worker accepts
  and sends the packet. Subsequent sends cannot overwrite that datagram;
  submitted bytes retain real network/error status through SocketPort.
- `kernel/fs/memfd.c`: Linux `memfd_create` publishes a **real tmpfs regular
  file**, pins the existing VFS inode through the ordinary File/FD, and
  unlinks its temporary creation path. Existing `ftruncate`, `pread`,
  `pwrite` and `MAP_SHARED` operate on that inode. Supports genuine
  `MFD_CLOEXEC`; `MFD_NOEXEC_SEAL` deliberately returns `EINVAL` (old Linux
  compatibility) because seals/noexec are not implemented. Huge pages and
  sealing requests explicitly fail, rather than pretending success.

Both new filesystem modules are in `scripts/full-kernel-core.sources`, the
x86 linker, and both native ARM64/RV64 build manifests. The common syscall
number adapter recognizes x86 `memfd_create=319` and generic RV/ARM `279`.

## Verification gates

- Native C host regression: `scripts/test-netlink-route.c` checks the exact
  Netlink binary messages, two concrete interface addresses, route port
  identity, PEEK/TRUNC, queue limits, poll and release.
- Native host syscall ABI: `scripts/test-lwip-socket-abi.c` exercises actual
  `socket(AF_NETLINK)`, bind, name, sendto and guest `msghdr` receive
  usercopy, plus the interface name/flags/MTU ioctl contracts.
- Native host VFS regression: `scripts/test-memfd.c` checks real creation
  helper admission and inode pin/unlink/close lifetime transitions; existing
  VM/VFS tests cover mutable tmpfs-backed file mappings.
- `scripts/check-lwip-foundation.sh`, `scripts/check-vfs-provider.sh`,
  `scripts/check-three-isa-core.py` and kernel C strict syntax checks.
- **Final acceptance still requires** M152 original dnsmasq→drill→independent
  host UDP DNS oracle (exact answer), original msmtp→host SMTP oracle and
  M155 original `xfs_repair` all real XFS phases in native **system-QEMU**.
  The success of host regression alone must never be labeled QEMU PASS.

This is a bounded compatibility increment, **not** a claim that all of
Linux Netlink or all `memfd_create` sealing semantics are implemented.
