# M204: broaden original Alpine userland compatibility without desktop work

This stage is **discovery**, not a claim of twelve passed applications or
Linux-syscall completeness. NaSeed preserves M194/M196/M197's real original
Alpine PID1, public HTTPS APK, OpenRC, network PCAP, physical ext4 and two
independent native-QEMU boots on x86-64, RV64 and AArch64.

## Scope and existing owners

- Continue to use `scripts/probe-m197-alpine-software-cohort.sh`, including its
  signed-root staging, M197 Bash/Lua/SQLite/Git transaction, live DHCP/DNS,
  second QEMU, host disk oracle and fsck. No new rootfs or QEMU runner.
- M204 is opt-in: `NAS_M204_COMPAT_COHORT=1` or
  `sh scripts/probe-m204-alpine-compat-cohort.sh amd64|riscv64|arm64`.
  Default M197 mode is unchanged.
- M204 explicitly rejects `NAS_M198_INTEGRATED_DESKTOP=1` and
  `NAS_M199_ORIGINAL_OPENRC_DESKTOP=1`. Existing fbdev/input support remains;
  no compositor, desktop protocol, DRM/KMS or GUI dependencies are added.
- Existing Linux `strace -f` works in the M72/M73 probes. Use its real guest
  trace for focused follow-up on a failing package, rather than defaulting to
  invasive always-on syscall tracing. The report also collects bounded
  `[MISS] syscall nr=N` kernel output (which is **not** an exhaustive trace).

## Cohort and evidence

`scripts/m204-alpine-cohort.tsv` enumerates twelve **candidate** packages by
functional category. It excludes previously verified M196 `tree`/`jq`, M197
`bash`/`lua5.4`/`sqlite`/`git` and earlier Python. A package can already be
installed as a dependency; that is explicitly recorded as `PREEXISTING`, not
claimed as newly installed.

The guest `tests/m204-software-workload.sh` writes the exact first-boot
`/root/m204-first-results.tsv` and rechecks the same packages during a
**second independent QEMU** into `/root/m204-reopen-results.tsv`. Each package
has three distinct measurements:

1. **install** — real `apk add` against signed official Alpine public indexes;
   `PASS`, `FAIL`, or `PREEXISTING`.
2. **functional** — a real workload (such as tmux PTY, nginx HTTP, Redis
   SET/GET, GNU make, OpenSSH key generation, rsync, procps) rather than a
   version-banner smoke test; `PASS`, `FAIL`, or `BLOCKED` when install fails.
3. **reopen** — on the second QEMU, verify the installed ELF is present and
   repeat the functional transaction; `PASS`, `FAIL`, or `BLOCKED`.

The host `scripts/m204-compat-report.py` rejects missing, forged, duplicate or
contradictory records. JSON output includes every package status, bounded
per-package failure-log tails, and observed unknown syscall numbers. It counts
a **new full pass only if all three phases are `PASS`**. Failed or preexisting
entries are never silently reclassified as passed or skipped.

Discovery mode reports the failures and exits successfully **only when all
evidence was collected**. Individual APK installs are time-bounded (150s)
and functional runs are time-bounded (45s). M204 changes only the inherited
M197 public transaction to regular `apk add`, which retains the official
signed package index on ext4; its subsequent installations reuse that index.
The standalone M197 lane still uses `apk --no-cache`. This avoids a redundant
`apk update`: in the first real M204 CI run (#13), that second index refresh
timed out on x86-64 and AArch64 *after* all four M197 programs had passed.
To require all twelve to pass, set
`NAS_M204_REQUIRE_ALL=1`; then the final host report returns nonzero if any
package is not a genuine new three-phase PASS. A package install timeout
is recorded as a real failure, not a falsely assigned mirror or ABI problem.
Use a fixed Alpine v3.23 repository and record artifacts per ISA.

## Safe host and QEMU gates

First run the fast no-QEMU contracts:

```sh
python3 scripts/test-m204-alpine-host.py
python3 scripts/test-m197-alpine-host.py
sh -n scripts/probe-m197-alpine-software-cohort.sh
sh -n scripts/probe-m204-alpine-compat-cohort.sh
sh -n tests/m204-software-workload.sh
```

The existing `m197-alpine-software-cohort` GitHub workflow has an optional
manual `compat_cohort=true` input. The M204 branch's push path now runs the
M204 cohort on all three architectures; manual `compat_cohort=false` retains
the M197-only lane with the **same** QEMU setup and upload-artifact owner.
Do not relaunch this
entire workflow repeatedly for individual ABI issues: first reduce a failed
original ELF into a minimal reproduction, compare with Linux on the same ISA,
fix the shared Task/VFS/MM/Socket/UAPI owner once, and only then rerun cohort.

### CI #14: observed results before the lifecycle fixes

Actual signed-Alpine/real-QEMU evidence from
[run 42028904661](https://github.com/yituanxing/na-seed/actions/runs/42028904661)
on commit `b066fdf`:

| Architecture | APK installations | First-boot functional | Second-boot functional | System clean unmount |
|---|---:|---:|---:|---|
| x86-64 | 11/12 | 9/12 | 8/12 | FAIL |
| AArch64 | 12/12 | 10/12 | 9/12 | FAIL |
| RV64 | 12/12 | 10/12 | 9/12 | FAIL |

Failures are independent of the M197 Bash/Lua/SQLite/Git baseline, which
still passed on the first boot. All three architectures reached
`M204_REOPEN_DISCOVERY_COMPLETE` after their second native QEMU; the kernel
then emitted `lwext4 final unmount/durable device flush failed`. **This is
not a full reboot PASS.** The earlier source collapsed pseudo-mount, ext4
unmount and physical flush errors into one line, so the precise owner remains
unconfirmed until the new stage/errno and mount-ref diagnostics run.

Observed common software misses: `iproute2` hits unsupported NETLINK
`SO_SNDBUF`; nginx is terminated; the second QEMU's procps test incorrectly
assumed a process named `init`, unlike the actual second-boot PID1 command.
The amd64 Redis installer also timed out; arm64 and RV64 Redis installed and
passed functional testing. Fix shared Linux ABI and owned service lifecycles,
not ISA-specific cases or synthetic success markers.

The following coherent follow-up is **host-verified but not yet confirmed by
fresh three-ISA QEMU**: bounded per-NETLINK socket `SO_SNDBUF`, actual lwIP
`RTM_GETLINK` plus IPv4/AF_UNSPEC address dumps, a PID1-independent procps
fixture, owned/reaped nginx/Redis processes, and precise pseudo/VFS/ext4/
BlockDevice unmount diagnostics. In particular, these changes do not waive
any clean-unmount requirement or reclassify past failed software as PASS.

The saved first and second UART records (rather than a missing JSON report)
are the source of the #14 per-package observations. The next decision is
based on their actual failure histogram, not on the number of Linux syscall
IDs recognized. No per-package kernel name checks or special return values
are permitted.

### CI #15: verified real three-ISA results, report-path regression

Commit `6136acff` was executed in
[run 42036971520](https://github.com/yituanxing/na-seed/actions/runs/42036971520).
All three QEMU jobs completed the new first-boot discovery and the independent
second-boot execution. Reading the *archived original UART logs* with the
existing strict `m204-compat-report.py` produced the following exact counts:

| Architecture | Signed APK installs | First-boot functional | Second-boot functional | Second-boot upstream ext4 unmount |
|---|---:|---:|---:|---|
| x86-64 | 11/12 | 10/12 | 10/12 | PASS |
| AArch64 | 12/12 | 11/12 | 11/12 | PASS |
| RV64 | 12/12 | 11/12 | 11/12 | PASS |

On every ISA, nginx now performs its real local HTTP transaction and exits
without pinning ext4; procps-ng also runs against a controlled live child.
All twelve real software tests were attempted in both QEMU invocations, and
the second guest emitted both `[LWEXT4 GUEST] clean upstream unmount and
durable flush` and `[PASS] cmdline userspace exit 0`.

However, the CI jobs themselves are **FAIL, not green**: the host report
accidentally passed the runner's `m197-batch.serial.log` path as the *first*
UART argument after the second QEMU had overwritten that path. It rejected a
missing `M204_FIRST_DISCOVERY_COMPLETE` despite the immutable saved
`m197-live.serial.log` containing that exact marker. Re-running the strict
reporter on the archived first UART and second clean log succeeded for all
three ISAs, with 10/12, 11/12 and 11/12 genuine new three-phase software
PASS, respectively. The subsequent fix points the first argument at the
immutable saved first UART. This fixes evidence bookkeeping, not any failed
application or underlying OS interface.

The sole shared remaining functional failure is `iproute2`, which moved from
the previously repaired `SO_SNDBUF` to a real `SO_RCVBUF: Protocol not
available` diagnostic. A bounded per-NETLINK-OpenFile `SO_RCVBUF`
implementation and host tests have been prepared, but are not yet verified
by a new genuine QEMU. AMD64 Redis has an additional 150-second installation
timeout: the original `addgroup`, `adduser` and Redis pre-install script all
returned zero, so attributing the later stalled APK process to the pre-install
script or inventing a missing syscall would be incorrect.

To reduce the cost of further kernel-only ABI iteration, an optional **manual
fast replay** path may use a disk checkpoint only after an earlier full cold
two-boot execution, real original APK installation, a clean second-QEMU
unmount and host ext4 check. It boots the *current* kernel in another real
QEMU and creates an entirely new test ledger. Its provenance and validation
are reported distinctly; **replay is not current-commit fresh HTTPS
installation/two-boot acceptance**. Branch-push strict cold acceptance
remains unchanged.
