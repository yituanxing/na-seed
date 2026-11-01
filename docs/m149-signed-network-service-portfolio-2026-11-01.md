# M149: 35 new original Alpine network/security/service ELF programs

**2026-11-01 status: original Alpine 3.23 software batch and native QEMU gate
prepared; guest QEMU acceptance is NOT yet established.**

This cohort broadens NaSeed's Linux-program compatibility test beyond HTTP
servers and MQTT into UDP management, DNS services, TLS utilities, authenticated
network clients, a full HTTP proxy, network-device diagnostics and s6 network
process helpers. It stages only original APK-signed unmodified Alpine 3.23
executables on the host; the guest runs those ELF files without GCC/Make.

| Family | Newly admitted independent ELF identities |
|---|---:|
| Net-SNMP daemons and clients | 13 |
| Knot authoritative DNS server and tools | 11 |
| GnuTLS clients/server/security tools | 5 |
| Tinyproxy, LFTP, ethtool | 3 |
| s6-networking TCP/time helpers | 3 |
| **Total** | **35** |

The official v3.23 `main/x86_64` APKINDEX was consulted and 9 exact upstream
APK files were inspected individually. All 35 selected paths were verified as
real ELF data with **35 different SHA-256 values**. In particular, M139's
`certtool`/`psktool` and M141's BIND, OpenSSH, Nmap, Iperf3 and TCPDump
binaries are excluded from the new-program count. Prior signed `curl` is used
only as a known client dependency, not recounted.

## Native acceptance contract

1. `scripts/stage-m149-prebuilt.sh` verifies no path duplicates any previous
   M-series inventory, signature-verified APK installation, correct target ELF
   machine type, and 35 distinct hashes per target architecture.
2. One real QEMU boot calls all 35 original executables through bounded
   version/help probes and requires the kernel's actual `[EXEC]` line for each.
   **These launch probes alone do not prove application functionality.**
3. SNMP: the real Alpine `snmpd` UDP agent must serve the numeric `sysUpTime`
   OID through both original `snmpget` and `snmpwalk` requests on loopback.
4. HTTP: the real Alpine Tinyproxy server must forward a signed Curl request
   from guest loopback to the independent Python host endpoint over the guest's
   genuine QEMU VirtIO-Net route. The host must log the actual forwarded GET,
   and the guest must receive the exact body bytes.
5. Native NIC attach (RV64/ARM64), DHCP lease, all unique guest markers,
   original ELF hashes, serial logs and VirtIO PCAP are hard gates; failure
   cannot be replaced by a host-only simulated success.

The default PR job uses native x86-64 QEMU; an explicit workflow dispatch or
`run-m149-multiarch` label uses the **same** workload on RV64 and ARM64 too.

## Expected ABI stress areas

- SNMP daemon UDP event wakeup, socket `bind`/`recvmsg` and timing/polling.
- Tinyproxy's TCP accept/connect, HTTP proxy request forwarding, FileOps
  lifecycle, child/thread behavior and backend socket close/reclamation.
- All 35 signed ELF dynamic-loader dependencies and startup syscalls.

This is intentionally a strict discovery gate, not a claim that these new
programs are all working. Physical NICs, the public Internet, TLS service
handshakes and multi-client performance are outside M149's present scope.
