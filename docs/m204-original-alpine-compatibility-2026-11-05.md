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
evidence was collected**. Individual APK installs are time-bounded (150s),
functional runs are time-bounded (45s), and the signed-index refresh is
time-bounded (120s), so one hung program does not erase later measurements.
To require all twelve to pass, set
`NAS_M204_REQUIRE_ALL=1`; then the final host report returns nonzero if any
package is not a genuine new three-phase PASS. An APK index-refresh failure
aborts the batch with **unknown root cause** (WAN/DNS/TLS/ABI/disk), not a
falsely assigned mirror problem or a software success. Use a fixed Alpine
v3.23 repository and record artifacts per ISA.

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
manual `compat_cohort=true` input. Its default push path continues to run
M197, while the manual mode runs this M204 cohort on all three architectures
with the **same** QEMU setup and upload-artifact owner. Do not relaunch this
entire workflow repeatedly for individual ABI issues: first reduce a failed
original ELF into a minimal reproduction, compare with Linux on the same ISA,
fix the shared Task/VFS/MM/Socket/UAPI owner once, and only then rerun cohort.

The next engineering decision is based on the actual `m204-compat-report.json`
failure histogram, not on the number of Linux syscall IDs recognized. No
per-package kernel name checks or special return values are permitted.
