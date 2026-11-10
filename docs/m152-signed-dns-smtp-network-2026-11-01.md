# M152: 35 new original Alpine network diagnostics and DNS/SMTP programs

## Package and program identity

M152 imports genuine, unmodified Alpine **v3.23** ELF files on the **host**
with the official APK keyring and `apk --no-scripts`. There is no compiler,
rebuild, generated guest binary or compatibility shim.

| Family | Distinct newly counted original ELF |
|---|---:|
| drill + ldns DNS tools | 14 |
| net-tools | 8 |
| iputils arping/tracepath | 2 |
| conntrack-tools | 3 |
| syslog-ng | 4 |
| rsyslogd | 1 |
| msmtp/msmtpd | 2 |
| socat procan diagnostic | 1 |
| **Total** | **35** |

The 35 paths exclude the entire previous `m*-original-elfs.txt` inventory;
hardlink/symlink aliases are not counted. All 35 selected x86_64 files were
examined from original official APK bytes, confirmed as ELF, and have different
SHA-256 hashes. The stage gate repeats path, architecture and real ELF SHA
checks independently on every requested target ISA.

Package closure: `drill`, `ldns-tools`, `net-tools`,
`iputils-arping`, `iputils-tracepath`, `conntrack-tools`,
`syslog-ng`, `rsyslog`, `msmtp` (community), `socat`,
plus previously admitted `dnsmasq` as a functional DNS server dependency.
The official v3.23 main/community indexes list all 11 direct package requests
with matching versions on x86_64, riscv64 and aarch64.

## Real functional acceptance

One native NaSeed QEMU boot per architecture stages and executes all **35**
original signed programs. Bounded `--help`/`--version` startup checks are
explicitly **LOAD ONLY**, not proof of application behavior; the host verifies
the kernel's actual `[EXEC] ... CPIO ELF` trace for all 35 paths.

Three independent semantic tests must all pass:

1. **LDNS real zone parser**: original `ldns-read-zone` must parse a
   deterministic SOA/NS/A zone file and produce the expected resource record.
2. **DNS UDP service / resolver**: original Alpine `dnsmasq` must receive
   original `drill`'s guest UDP A query on loopback port 15454, forward it
   through actual QEMU VirtIO-Net to an independent host Python DNS server
   at `10.0.2.2:15453`, and return the authentic A answer
   `198.51.100.152`. The host independently logs the exact received
   question and the emitted DNS answer.
3. **SMTP TCP transaction**: original Alpine `msmtp` connects to the
   independent host SMTP service `10.0.2.2:15425`, completes real
   EHLO / MAIL FROM / RCPT TO / DATA / QUIT dialogue and sends a tagged
   message. Host verification demands the envelope and exact Subject/body
   before recording `[M152 HOST SMTP DELIVERED]`.

The protocol gate requires both the exact guest semantic markers **and**
independent host oracle markers. The host also archives raw VirtIO PCAP and
guest clean/raw serial logs. An observed TCP connect, fabricated response,
guest echo or help output cannot satisfy either protocol.

Additional original `ifconfig -a` and `netstat -an` queries report honest
optional kernel support but cannot substitute for the semantic gates.

## Multi-ISA CI and expectations

The default PR job runs x86-64 first. A manual workflow dispatch, the
`run-m152-multiarch` label, or the consolidated
`run-m151-155-multiarch` label enables unchanged RV64 and ARM64 native
QEMU workflows. Each architecture stages target-native signed packages,
uses its own real VirtIO NIC (PCI on x86, modern MMIO on the others), DHCP,
host oracle and native PCAP.

**Status: scripts prepared; no M152 QEMU PASS is claimed until the real
GitHub Actions native jobs produce the required serial and independent host
markers.** Potential discovery areas include dnsmasq's Linux interface
ioctls and UDP forwarding, msmtp's socket state, and LDNS parser options.
Any kernel ABI issue must be repaired in the real kernel and retested; the
application ELF and semantic gates remain unchanged.
