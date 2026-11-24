# NaSeed

**An operating system grown by real software.** NaSeed is an independently
developed kernel, advanced by running *unmodified* executable programs and
realistic device protocols rather than adding syscall or driver stubs only to
satisfy a test. It is distinct from the NaOS reference project.

## Current development status (2026-11-23)

**Important:** This summary describes **unmerged development branches**,
not the default `main` branch or a physical-PC release. Each acceptance
claim belongs to its own exact Git commit and CI run.

### Native userspace / distributions

The [userspace development PR #148](https://github.com/yituanxing/na-seed/pull/148)
covers x86-64, AArch64 and RV64 system-QEMU acceptance of real Linux ABI
behavior and original signed distribution binaries. An earlier exact clean
head `b3ea3e8` passed the [M194 original Alpine / OpenRC / ext4
persistence test](https://github.com/yituanxing/na-seed/actions/runs/41844206289)
on all **three native ISAs**: original Alpine init/login/services, guest
write and sync, a second separate QEMU session against the *same* persistent
ext4 disk, and independent host verification. It is not an assertion that
every Linux syscall, application, Debian service or new commit is qualified.

Additional Debian sysvinit/glibc, console and device tests are tracked by
that PR. They have independent current-head gates; a historical M194 pass
must not be promoted to a later branch's all-green result.

### QEMU hardware-driver qualification

The [hardware PR #150](https://github.com/yituanxing/na-seed/pull/150)
is stacked on the userspace branch. On exact hardware HEAD
`ed3ea2df4794aa6b77e31466428cd5f166f1397b`, the 2026-11-10
GitHub Actions `H0`–`H15` workflows completed **successfully** for
their individual, explicitly bounded test profiles. Their actual QEMU
device, program identity, negative-control and teardown requirements are
defined by each workflow. The hardware scope includes:

| Milestones | Observed device/software path |
| --- | --- |
| H0–H2 | Firmware and PCI/PCIe discovery, read-only resource inventory |
| H3–H7 | QEMU NVMe Identify/LBA and read-only ext4; original Debian BusyBox/Bash execution |
| H8 | Original signed Alpine musl/BusyBox/Lua/jq/SQLite from NVMe ext4 |
| H9 | Q35 AHCI/SATA DMA read-only ext4 and original Alpine applications |
| H10 | PCI E1000, lwIP and original Alpine BusyBox HTTP client |
| H11 | xHCI USB Mass Storage ext4 and original Alpine applications |
| H12–H14 | xHCI genuine USB keyboard/mouse and concurrent `evtest` events |
| H15 | QEMU FT232BM USB serial to `ttyUSB0`, original signed Alpine BusyBox I/O, host nonce verification |

Examples of exact-head evidence:
[H8 NVMe](https://github.com/yituanxing/na-seed/actions/runs/42864260849),
[H9 SATA](https://github.com/yituanxing/na-seed/actions/runs/42864260867),
[H10 E1000](https://github.com/yituanxing/na-seed/actions/runs/42864260739),
[H14 dual HID](https://github.com/yituanxing/na-seed/actions/runs/42864260712)
and [H15 USB serial](https://github.com/yituanxing/na-seed/actions/runs/42864261033).

**The boundary matters:** these are controlled QEMU/OVMF device paths,
mostly with read-only backing media and polling. They do **not** qualify
arbitrary physical NVMe/SATA/USB/NIC hardware, IOMMU/interrupt/hotplug
coverage, a generic hub driver, writable physical storage, unrestricted
Alpine/Debian compatibility or a desktop on real hardware. In particular,
the H11 USB storage acceptance is deliberately read-only.

### Long-term maintenance contract

1. Make each PR-head test **reproduce its actual hardware/software path**
   and record the exact SHA, original executable provenance, exit status,
   device absence negatives, cleanup and disk checks. A host-only marker
   is not equivalent to a real guest execution.
2. Separate **kernel/ABI compatibility**, **device-driver functionality**,
   **distribution boot/services** and **physical hardware qualification**.
   Do not upgrade a passing lower-level gate to the next level.
3. Fix classes of observable errors at the correct owner
   (syscall, VFS, MM, scheduler, PCI/DMA, driver or userspace setup).
   Preserve cross-ISA and previously accepted device regressions.
4. Keep the OS independently buildable with its existing pinned/GNU
   toolchains. MiniC/MiniAS/MiniLD end-to-end integration is an additional
   cross-project gate, never a prerequisite that blocks independent NaSeed
   bug fixes.
5. Preserve the PR dependency chain; do not merge a hardware descendant
   to `main` while its userspace base and provenance/licensing review are
   unresolved.

## Documentation and historical records

Current architecture, ABI, ownership, provenance and acceptance contracts remain in
`docs/`. Earlier milestone reports are available in the
[historical milestone index](docs/cleanup-01-baseline.md#historical-milestone-archive).
The archive preserves exact reports and their dated evidence, not current-head PASS claims.

## Original development contract and early milestones

The original project deliberately started with x86-64 and static GNU
Bash before extending into three ISAs, dynamic ELF programs, distribution
services and hardware drivers. The early development steps remain useful
as a historical regression sequence:

1. Boot a real kernel under Limine and QEMU, with serial diagnostics.
2. Add an executable userspace ELF and validate the Linux x86-64 syscall ABI.
3. Run a fixed, statically linked GNU Bash and fix **classes of observed
   failures**, not isolated test cases.
4. Keep integration tests and prior milestones green; distinguish missing
   syscalls, incorrect syscall semantics and CPU exceptions.
5. Add genuine hardware/virtual-device driver support as real applications
   demand it.

Early milestones were M0 (boot + memory + serial), M1 (ELF + ring 3 +
write/exit), M2 (Bash -c), M3 (interactive Bash), then external commands,
pipes and drivers. Those identifiers describe historical work, not the
current upper bound on the system.

### Historical M4.6 BusyBox argv probe

The optional Limine `executable_cmdline_request` entry
`cmdline: naseed.argv=/bin/busybox uname -s` passes the userspace
argv without recompiling a program for every test. Single/double quotes
and backslash escaping group arguments; this is not a shell. Malformed
nonempty cmdline is rejected.

`make alpine-argv-probe` checks the same original Alpine BusyBox
binary against `uname -s`, `pwd` and `id -u` with independent boot
configurations, guest stdout, exit codes and host QEMU status. This was
an early milestone and must not be confused with current distribution
acceptance.
