# NaSeed M145: RV64 and ARM64 original Alpine HTTP/RESP services

**2026-11-01 — native QEMU 4/4 jobs PASS; all M143 and M144 x86-proven
service workloads also PASS on RISC-V 64 and AArch64.**

The same unmodified original signed Alpine 3.23 executable files, scripts,
APK provenance policy, and independent HTTP host oracles are used for each
architecture. No guest ELF is rebuilt, shimmed or replaced to improve scores.

| Architecture | M143 Wget, aria2c, lighttpd | M144 ab, nginx, haproxy, Valkey Server/CLI |
|---|---|---|
| x86-64 (existing) | PASS | PASS |
| RISC-V 64 (new) | **PASS** | **PASS** |
| AArch64 (new) | **PASS** | **PASS** |

- [M145 four-job QEMU evidence — run 41306406522](https://github.com/yituanxing/na-seed/actions/runs/41306406522)
  - RV64 M143 original software: SUCCESS; host wire, DHCP, original ELF traces
    ([job 124847587455](https://github.com/yituanxing/na-seed/actions/runs/41306406522/job/124847587455)).
  - ARM64 M143 original software: SUCCESS
    ([job 124847587558](https://github.com/yituanxing/na-seed/actions/runs/41306406522/job/124847587558)).
  - RV64 M144 original software: SUCCESS
    ([job 124847587510](https://github.com/yituanxing/na-seed/actions/runs/41306406522/job/124847587510)).
  - ARM64 M144 original software: SUCCESS
    ([job 124847587654](https://github.com/yituanxing/na-seed/actions/runs/41306406522/job/124847587654)).
- This expansion is **eight distinct already-known original programs across
  two more ISAs** — 16 newly demonstrated ISA-program pairs, **not 16 newly
  discovered or imported ELF program identities**.
- Full per-job artifacts preserve exact native guest serial, signed binary
  hashes, host HTTP request counts and VirtIO-MMIO PCAP; each job checks
  a real QEMU NIC, DHCP lease, all guest semantic PASS markers, original
  `EXEC` records, and independent host HTTP/HAProxy GETs.
- Original Ncat MMIO/DHCP/TCP smoke passed on RV64 and ARM64 before M145.
  This proves substantially more of user-space and concurrent server
  compatibility than the old one-client probe, not generalized Linux parity.

## Implementation and acceptance boundaries

- One shared `scripts/probe-m145-service-multiarch.sh` runner, parameterized by
  target architecture and existing signed cohort. The native RV64/ARM64
  kernel builders and QEMU BusyBox matrix runners remain unchanged.
- 2 architectures × 2 authentic program cohorts = 4 narrow independent jobs.
- Each job starts a real Python host-side HTTP oracle, verifies host-side wire
  requests and guest-visible contents. No synthetic guest networking replaces
  upstream lwIP or pinned U-Boot VirtIO-MMIO.
- M143 client downloads and signed OpenSSL SHA-256, guest lighttpd HTTP
  loopback, M144 ApacheBench four actual GETs, guest nginx static server,
  HAProxy gateway proxy response, and guest Valkey RESP PING/SET/GET are
  required unchanged. A missing marker/ELF/host HTTP proof fails the job.
- The guest is tested on QEMU user-network and loopback, **not** an Internet
  routable physical NIC, TLS server, sustained multi-client performance or
  all Linux socket options. Eight simultaneous Netconn resources remain a
  documented current upper bound; no fictitious scale-up was claimed.

## Next work

Increase the **unique original signed Alpine ELF pool** using genuinely
new application/server binaries rather than re-counting preexisting targets.
Use the same small-group QEMU process for new syscall/ABI gaps, and only run
a full cross-ISA matrix when there is a coherent new batch worth verifying.
