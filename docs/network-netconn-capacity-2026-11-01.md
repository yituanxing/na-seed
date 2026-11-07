# NaSeed lwIP Netconn capacity: host proof and QEMU boundary

**2026-11-01 — host-only verified; cross-ISA QEMU pending.**

The original M145 QEMU network gates proved HTTP and RESP services with an
eight-Netconn resource ceiling. This follow-up increases *concurrent network
connection objects* without increasing the kernel's fixed eight-entry native
mailboxes or the bridge's eight-command work queue.

| Resource | Previous | Candidate | Reason |
|---|---:|---:|---|
| Bridge active Netconns | 8 | 32 | Admission follows actual lwIP pool |
| `MEMP_NUM_NETCONN` | 8 | 32 | Official upstream allocations |
| TCP/UDP active PCB pools | 5 / 4 | 32 / 32 | Avoid accepting handles without protocol PCBs |
| TCP listen PCB pool | 8 | 32 | Multiple listeners |
| Kernel mailbox / bridge work queue | 8 / 8 | 8 / 8 | Hard queue capacity remains unchanged |

The `NAS_LWIP_BRIDGE_ACTIVE_LIMIT == MEMP_NUM_NETCONN` compile-time assertion
guards against contradictory admission and upstream pool limits. TCP segments,
PBUFs and the lwIP heap were also increased to support the larger population.

## Verified now

Run `sh scripts/check-lwip-foundation.sh` on a host with GCC and the pinned
upstream lwIP submodule. This is a real lwIP host execution test, not a mock
allocation counter:

- 32 UDP Netconns and 32 TCP Netconns can be opened in separate rounds;
  admission #33 returns `-ENFILE`.
- Eight asynchronously closed objects cannot be prematurely reused; after
  native close completion, the same File slots reopen with new generations.
- Twelve concurrent loopback TCP client/server pairs exchange exact payloads
  with distinct accepted OpenFile identities (24 endpoints + one listener).
- The complete existing SocketPort, syscall/usercopy, FD lease, DHCP/native
  Netif and network build-selection host regressions still pass.

## Not yet proved

- No new RV64/ARM64/x86-64 QEMU job has yet certified this raised limit.
- No real physical NIC, routed external Internet, TLS or sustained performance
  claim follows from the host tests.
- The single physical/virtual NIC and fixed-size per-mailbox queue remain
  separate limits. Increasing MEMP pools does **not** increase those queues.

Run the existing M145 original-service cross-ISA gates on the new head, then
add a dedicated native QEMU multi-peer pressure gate if applications expose a
kernel or IRQ issue. Do not re-label the old M145 evidence as new capacity
validation.
