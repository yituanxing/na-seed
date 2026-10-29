# NaSeed native source-build profiling (M38/M55/M56)

The **baseline is real guest software**. M38 compiles three original C targets
with Alpine GCC, M55 configures/builds/rebuilds upstream cJSON using CMake,
Ninja, GCC and CTest, and M56 builds original BusyBox with real GNU Make/Kbuild.
`tests/native-perf-clock.sh` already emits per-phase **guest** centiseconds
from `/proc/uptime`; these are not host provisioning time. Existing acceptance
markers and fail-closed checks stay authoritative.

## Measurement sources and limits

| Layer | Source | What can be claimed now |
| --- | --- | --- |
| Source build | `NAS_PERF_PHASE` in M38/M55/M56 serial | Verified command-group elapsed guest time when all original acceptance markers pass |
| CPU occupancy | `tests/native-perf-clock.sh` sources the staged `native-profile-guest.sh` at the **same original phase boundaries**, reading real guest `/proc/stat` | Automatically emits busy/idle 100-Hz task occupancy and context-switch deltas during native full M38/M55/M56; **not** actual CPU cycles or separate user/kernel time |
| Syscall | Optional genuine guest `strace -f -c` and `scripts/native-profile-strace.py` | Instrumented syscall counts/summary time; perturbs execution, must be a separate diagnostic run |
| VFS | Future opt-in kernel counters, e.g. lookup calls/ns, mount crossings, provider calls | **not-measured** until emitted by the actual guest; no inferred timing from build wall time |
| Host setup, QEMU boot, rootfs image construction | Runner/job logs | Keep separate from guest build phases; do not merge them into source-build numbers |

`scripts/native-perf-report.py` understands optional immutable counter records:

```
NAS_PERF_COUNTER workload=m56 scope=cpu metric=kbuild.busy value=120 unit=cs
NAS_PERF_COUNTER workload=m56 scope=cpu metric=kbuild.idle value=10 unit=cs
NAS_PERF_COUNTER workload=m56 scope=cpu metric=kbuild.switches value=42 unit=count
NAS_PERF_COUNTER workload=m56 scope=syscall metric=kbuild.total.time value=5000000 unit=ns
NAS_PERF_COUNTER workload=m56 scope=vfs metric=kbuild.lookup.time value=400000 unit=ns
```

Accepted scopes are `cpu`, `syscall`, `vfs`, units `ns`, `us`, `cs`, `count`,
`bytes`. A missing scope prints `not-measured`. Duplicate records, malformed
units, or unequal counter availability across A/B samples fail the report.
This protocol is an **ingestion contract** for future syscall/VFS counters;
neither source exists at the current HEAD. The three `scope=cpu` values are
now produced by the guest phase hook, subject to a successfully readable,
monotonic NaSeed `/proc/stat`. No other metric is inferred from wall time.

## Real full-guest CPU profile and optional diagnostics

`rootfs/alpine/build.sh` stages both `native-perf-clock.sh` and
`native-profile-guest.sh` under `/tests`. The unchanged M38/M55/M56 guest
drivers already call `nas_perf_begin` and `nas_perf_phase`; those same calls
now automatically delimit **whole** GCC/CMake/CTest/BusyBox phases, including
M56 Kconfig's multiple original commands. On a successful phase they emit
the original `NAS_PERF_PHASE` plus three `NAS_PERF_COUNTER scope=cpu` records.
The commands, exit statuses, and original acceptance markers are unchanged.

The kernel's `kernel/fs/procfs.c` places monotonic task occupancy in the
otherwise-Linux-`system` field of its single-CPU `cpu` row (`0 0 busy idle`);
`kernel/task/accounting.c` includes active task runtime, not precise
instructions or separate user/system/IRQ CPU time. The sampler requires that
specific NaSeed field arrangement; it rejects ordinary Linux `/proc/stat`
when nonzero user/nice fields would make that interpretation false. A missing,
malformed, or nonmonotonic snapshot silently **omits** counter lines; it
never substitutes zero, fails a correct original workload, or manufactures
an application's PASS marker. The report must label absent metrics
`not-measured`.

For a separately scoped command instead of an entire stage, the same guest
sampler can wrap a **single original command** without changing its arguments:

```sh
sh /tests/native-profile-guest.sh m56 kbuild -- \
  /usr/bin/make -j2 HOSTCC=/usr/bin/gcc CC=/usr/bin/gcc busybox
```

The rootfs builder already stages the script. The wrapper emits counters
only when the command succeeded and snapshots were monotonic. This CPU
accounting includes other scheduled work; compare identical workloads and
quiescence. Do not mix an instrumented run with an earlier uninstrumented run.

For a **separate, intentionally instrumented** run, capture original Alpine
`strace -f -c` of a representative command (verify it actually works inside
NaSeed), then on the host:

```sh
python3 scripts/native-profile-strace.py --workload m56 --phase kbuild \
  --input guest-strace-summary.txt --syscalls read,write,openat,wait4
```

Only syscall names present in the genuine summary are accepted. Merge its
counter lines into the serial log of the **same instrumented run** if you want
comparisons. Do not compare strace-instrumented and ordinary runs as if they
were identical; syscall time is not exclusive VFS time or CPU time.

## Matched A/B requirements

Capture two or more independent successful runs **per arm**, ideally three or
more, without suppressed failures. Include one sidecar JSON manifest for each
serial log. Required keys (example values are placeholders, not results):

```json
{
  "workload": "m56",
  "port": "riscv64",
  "qemu_version": "record exact qemu-system-riscv64 --version",
  "qemu_machine": "virt,acpi=off",
  "qemu_args": "record normalized complete guest run arguments",
  "rootfs_sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "source_sha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "toolchain": "record exact guest gcc/make/binutils/musl versions",
  "build_flags": "record original guest compile/configure flags",
  "cache_state": "cold",
  "guest_ram_mib": 512,
  "vcpus": 1,
  "instrumentation": "phase-cpu-procstat"
}
```

Only `cache_state=cold|warm|controlled` is accepted; include evidence of
what that choice means. Baseline/candidate commits are supplied separately
because a kernel change is precisely the variable being tested.

```sh
python3 scripts/native-perf-report.py --workload m56 --port riscv64 \
  --baseline-ref BASE_SHA --candidate-ref NEW_SHA \
  --baseline base-run1.clean.log base-run2.clean.log \
  --candidate new-run1.clean.log new-run2.clean.log \
  --baseline-manifests base-run1.json base-run2.json \
  --candidate-manifests new-run1.json new-run2.json \
  --strict-metadata
```

The reporter refuses mismatched QEMU, source, rootfs, toolchain, CPU, RAM,
cache, instrumentation or flags. If manifests are absent, ratios are explicitly
marked **UNVERIFIED COMPARABILITY**. Never infer a kernel speedup from a host
CI timeout or from a single test.

## Next CI execution requested from the integrator (not executed here)

1. Pin one exact HEAD and collect repeated `M38`, `M55`, `M56` full native
   QEMU serial runs for amd64/riscv64/arm64, keeping `fast` PR acceptance
   separate from explicit `full` source-build dispatch.
2. Attach actual run-manifests and clean logs, including guest source and
   rootfs digests, QEMU executable/arguments, guest toolchains and cache state.
3. In full guest logs, verify all three CPU counters accompany **every**
   original `NAS_PERF_PHASE` for each successful ISA/phase. Check missing or
   malformed snapshots honestly as `not-measured`; keep original acceptance
   identical and quantify the phase sampler's overhead.
4. Use separate original strace diagnostics only if real `-f -c` succeeds.
   Instrumentation changes timings. VFS hotspots require bounded new kernel
   producers in a separately reviewed change, not guesses from elapsed time.
5. **Do not optimize volatile-byte `kernel/lib/string.c` yet**: it is a
   candidate hotspot, not a measured bottleneck. Use authentic build phases,
   copy-volume/CPU evidence and A/B before touching portable freestanding
   primitives.

Host-only contract checks: `python3 scripts/test-native-perf-report.py`,
`python3 scripts/test-native-perf-profile.py`, plus `sh -n` on the new guest
sampler. These do not prove any NaSeed guest run or performance gain.
