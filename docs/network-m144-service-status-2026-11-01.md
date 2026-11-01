# NaSeed M144 — signed native server cohort (2026-11-01)

**Final status: 4/4 independent real x86 QEMU cases PASS, 5/5 newly
admitted distinct original Alpine v3.23 signed ELF executables PASS.**

All workloads use NaSeed's genuine guest VirtIO-Net/lwIP sockets and original
unmodified Alpine binaries. We do not count merely installing, starting, or
printing a version as passing software compatibility.

| Original ELF(s) | Independent runtime evidence | Status |
|---|---|---|
| `/usr/bin/ab` | Four completed HTTP requests with no errors, plus exactly four host oracle GET records | PASS |
| `/usr/sbin/nginx` | Starts guest-native HTTP listener; signed curl receives byte-identical static file | PASS |
| `/usr/sbin/haproxy` | Guest loopback frontend accepts curl, proxy forwards `/haproxy` to 10.0.2.2, host logs actual GET, and original curl checks full returned response | PASS |
| `/usr/bin/valkey-server`, `/usr/bin/valkey-cli` | Real guest TCP RESP PING / SET / GET with exact value | PASS |

- **M144 signed ELF import**: 5 different target SHA-256 identities; PASS.
- **M144 full x86 QEMU + host oracle**: [Actions run 41305277016](https://github.com/yituanxing/na-seed/actions/runs/41305277016); SUCCESS.
- **Native upstream lwIP, Netconn, SocketPort and syscall ABI**: PASS in the same run.
- **Independent M143 original Wget/aria2c/lighttpd**: PASS on M144's source head.
- **No full GNU/Linux or physical network claim**: these are bounded original application tests in QEMU slirp and guest loopback; not network performance, DNSSEC, SSH server, hardware-driver or complete TCP option compatibility certification.
- [Draft PR #148](https://github.com/yituanxing/na-seed/pull/148) remains unmerged.

## Actual compatibility defects discovered through the programs

1. A last-close native Netconn still owns its original OpenFile index during
   asynchronous teardown. Socket admission now skips this index until the
   worker frees it, instead of reporting a false `ENFILE`; the active Netconn
   cap was not raised to conceal the race (M143).
2. Nonblocking TCP `connect()` and immediate `TCP_NODELAY` must preserve the
   same FileOps control ownership, then apply the option on the **real upstream
   PCB**, not fail with `EBUSY` (Valkey).
3. Repeated nonblocking TCP `connect()` after establishment must return
   `EISCONN` rather than starting another handshake (ApacheBench).
4. HAProxy uses `sendto(MSG_DONTWAIT | MSG_NOSIGNAL | MSG_MORE)` to return
   backend response bytes to its client. The initial guest trace showed actual
   host requests but `EOPNOTSUPP` when sending its TCP response, causing
   curl's `Empty reply from server`. `MSG_MORE` is a TCP coalescing hint,
   not a transport record boundary: the shared native FileOps stream can
   transmit accepted bytes promptly without preserving the exact packetization.
   **UDP `MSG_MORE` datagram corking remains unsupported** and explicitly
   fails rather than silently changing datagram boundaries.
5. Genuine original server startup requires writable runtime directories,
   real user records and HAProxy's documented `noreuseport` configuration
   for the currently unsupported multi-listener optimization. The host
   request and client response checks remain strict.

## Historical FAIL observations (retained as regression context)

- [41303412089](https://github.com/yituanxing/na-seed/actions/runs/41303412089):
  3/4 runtime cases, 4/5 ELF identities.
- [41304884824](https://github.com/yituanxing/na-seed/actions/runs/41304884824):
  nine genuine HAProxy backend requests recorded, but client got an empty
  reply because `MSG_MORE` was still rejected. This was not a listening or
  host networking failure.
- [41305277016](https://github.com/yituanxing/na-seed/actions/runs/41305277016):
  fixed TCP reply flags, successful original frontend/backend/client end-to-end.

Next expansion should batch original service/client ELF targets and add
cross-architecture workload runs, with this M144 QEMU gate kept as
independent network regression protection. It must not be weakened into
a version-print, localhost-only proxy stub, or empty output marker.
