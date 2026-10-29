# CI proof types and M88–M103 coverage contract

This page describes what **actually runs**, not how complete a milestone name
sounds. The smoke workflow at `.github/workflows/ci.yml` is a useful early
regression filter, but it does not replace system-emulator acceptance.

| Proof label | Evidence produced | Evidence **not** produced |
| --- | --- | --- |
| `[HOST]` | Compile and execute a small fixture as a CI-runner host process | Target-ISA compilation; native guest boot; actual trap frame transitions |
| `[HOST + TARGET-COMPILE]` | Execute a host fixture; compile kernel source or assembly to RISC-V 64 and AArch64 objects (with x86-64 host object/execution coverage) | Link/boot a complete target OS or execute the target-ISA test in system QEMU |
| Genuine system-QEMU jobs | Boot NaSeed on an actual emulated ISA and check an original guest ELF/process/serial outcome | Automatic guarantee that a manually triggered source-build suite ran on the **current** commit |

The `[Mxx TARGET PASS]` lines printed by the M91–M102 shell scripts refer to
**cross-compiler acceptance only**. Those lines are not system-QEMU PASS lines.
They are intentionally given the narrower `[HOST + TARGET-COMPILE]` CI title.

## Exact M88–M103 automated smoke mapping

| Milestones | CI check scripts | Actual proof |
| --- | --- | --- |
| M88, M89, M90 | `check-m88-exceptions.sh`, `check-m89-poll.sh`, `check-m90-io-wait.sh` | Host fixture only |
| M91–M97 | `check-m91-kcontext.sh` through `check-m97-k-timeouts.sh` | Host fixture plus RISC-V/AArch64 object/assembly compilation |
| M98 **and M99** | **One** `check-m98-sched-core.sh` invocation; `test-m98-sched-core.c` has independent ring-cursor M99 assertions | Shared host scheduler policy and ring scan; target objects, **not** native IRQ frame parity |
| M100–M102 | `check-m100-signal-failure.sh`, `check-m101-cow.sh`, `check-m102-tty-cow.sh` | Host fixture plus RISC-V/AArch64 object compilation |
| M103 | `check-m103-thread-tid-lifecycle.sh` | Three **host** fixtures (clone/TID policy, Linux oracle, guest-source compiled as host), plus x86-64/RISC-V/AArch64 process and scheduler **object compilation**. No QEMU guest execution |

Before this correction, M99 and M98 appeared as two separate green CI steps
although both invoked exactly `check-m98-sched-core.sh`. Removing the duplicate
does **not** remove M99 ring-scan assertions: they remain in the shared host
fixture. There is still **no** dedicated three-ISA system-QEMU verification of
M99's user IRQ saved-frame commit/restore in this smoke check. A future gate
must actually boot and exercise timer IRQ preemption, save/restore of original
user registers, return to user mode, and starvation/fairness on all three ISAs.
It must verify guest serial/exit evidence and reject a missing outcome.

`scripts/check-ci-proof-map.py` runs automatically in the smoke workflow. It
fails the job when an M88–M103 check is omitted, repeated, given an inflated
proof label, or no longer contains its audited host/target-compile actions.
It also checks that M50 full and M56/M57 remain explicit manual workflows.
For M103, `NAS_M103_HOST_ONLY=1` is an explicit **local-only** fallback; CI
must not set that environment variable and is expected to fail if Clang cannot
compile the three target objects. The guest-source fixture run on the host is
not evidence of guest execution in native system-QEMU.
The checker has mutation-based self-tests:

```sh
python3 scripts/check-ci-proof-map.py --self-test
```

## Large program / multi-architecture release gates

Three deep gates have different meaning from the above smoke checks:

- **M50 automatic PR** runs the `fast` matrix: original prebuilt Linux ELF
  programs in system-QEMU. It is **not** the full in-guest GCC/Make source-build
  matrix.
- **M50 `workflow_dispatch`, `mode=full`** runs 3 ISA × 4 suites
  (`foundation`, `pthread`, `lifecycle`, `parallel`) with original in-guest
  GCC/CMake source builds and retained build timings.
- **M56 / M57 `workflow_dispatch`** are separate three-ISA in-guest upstream
  BusyBox Kbuild and SQLite configure/Make/SQL gates respectively.

Success of any of these workflows on an older commit is a historical
capability result, **not an exact-head validation**. For major scheduler,
exception, MM, VFS or performance batches, record the PR head SHA, all three
manual workflow run IDs and their individual three-ISA job conclusions before
claiming the *current* head is fully accepted. Do not silently run or relabel
the fast matrix as the full matrix.
