# Alpine acceptance orchestration boundaries

The original signed Alpine packages, original `/sbin/init` / OpenRC, real
native-QEMU first and reopen boots, host TCP/UDP protocols, two independent
NIC PCAPs, and physical ext4 disk oracles are the acceptance evidence. A
green host contract does **not** mean that an Alpine workload passed QEMU.

Only these three shared host-side concerns live in this directory:

- `profile.sh` parses the 27 historical `NAS_Mxxx_*` cohort selectors before
  any guest build or host launch. It retains their legacy variables and
  incompatibilities, including the explicit `M239` + `M233` owner pair.
- `qemu-netdev.sh` validates and composes the slirp TCP/UDP hostfwd rules,
  independent second network, and optional PCAP. The three ISA launchers
  continue to own their machine and VirtIO device models. Invalid host
  forwarding input is rejected before QEMU starts.
- `host-supervisor.sh` owns the M239 host-observer EXIT/signal lifecycle.
  Register each direct child immediately after `$!` and unregister it after
  `wait`. Cleanup terminates only registered observers and retains all
  UART, PCAP, receipt, and ext4 artifacts for forensic review.

The existing `probe-m197-alpine-software-cohort.sh` remains the orchestration
owner for **real** Alpine work. Do not move guest workload logic, signed
package closure, host protocol verification, or final disk/PCAP oracles into
these helpers. In particular, do not replace the second physical QEMU boot
with a replay or host-only result while claiming native acceptance.

`acceptance-architecture-contract.yml` runs fast, exact-commit host tests
for these shared contracts; the M239 native three-ISA workflow is still the
separate final acceptance gate.
