# Alpine acceptance orchestration boundaries

The original signed Alpine packages, original `/sbin/init` / OpenRC, real
native-QEMU first and reopen boots, host TCP/UDP protocols, two independent
NIC PCAPs, and physical ext4 disk oracles are the acceptance evidence. A
green host contract does **not** mean that an Alpine workload passed QEMU.

The initial shared host-side orchestration concerns are:

- `profile.sh` parses the 27 historical `NAS_Mxxx_*` cohort selectors plus the
  independent M240 SSH PTY selector before any guest build or host launch. It
  retains all historical variables and incompatibilities, including the
  explicit `M239` + `M233` owner pair. M240 is mutually exclusive with M230;
  the two share only the native-QEMU runner plumbing, not their key policies.
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
for these shared contracts. M239 and M240 have distinct native-QEMU workflows;
neither host-only contract is a substitute for the relevant true two-boot
guest NIC, serial, original OpenRC and physical ext4/fsck final oracle.

`check-batch-markers.sh` owns the exact-once per-program marker and batch-summary gate for migrated M109–M157 probes. It does not launch QEMU or replace EXEC, codec, networking or filesystem assertions. Each caller retains its original manifest, count and runner. `test-batch-markers.py` exercises the real checker against all migrated manifests plus fail-closed negative cases; this is host evidence only.

`check-elf-history.sh` and `hash-original-elfs.sh` consolidate host-only historical path and digest checks. Counts, distinctness modes, artifact paths, original package checks, fixture checks and stronger per-batch ELF validation remain with the callers. `test-elf-admission.py` checks the actual helpers and repository manifests; no guest acceptance is inferred.

`../alpine-first-uart.py` holds the fourteen first-boot command profiles that
previously had separate UART adapters. It invokes the same M194 UART runner
through M205/M197. The independent second boot, host-wire, signed-package and
physical disk verifiers are not replaced. The five simple disk profiles use
`../m206-alpine-disk-check.py PROFILE PORT SIGNED_ROOT DISK`; specialized
application, key-cleanup and network persistence checks retain their owners.

For M112–M116, M119–M122, M124–M133, M135–M136 and M139, the original signed ELF manifest is the sole source of the one-to-one expected per-program marker names. `profile_markers.py` derives these deterministically; no guest logs or pass markers are used to construct expectations. Profiles with aliases, nontrivial marker names or additional assertions keep authored manifests. The host regression exercises all resulting marker sets and missing/duplicate/summary-only negative cases.
