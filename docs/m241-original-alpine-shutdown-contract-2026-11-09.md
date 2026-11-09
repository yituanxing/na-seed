# M241 — original Alpine BusyBox shutdown, not a host-injected exit

## Motivation and prior evidence

The immutable M240 three-ISA proof at run 42556564137 confirms original
Alpine PID1/OpenRC, two physical ext4 QEMU boots, and nine genuine SSH PTYs
per boot. That proof does **not** claim that a guest `poweroff`/`reboot`
command can shut down or reset NaSeed. Previously the acceptance harness
ended the guest from the host after completing its workloads.

## Original Linux userspace path

The unmodified BusyBox `halt` family signals PID1, which blocks these
signals and consumes them via `sigtimedwait()`:

| Command | Signal to PID1 | Final Linux `reboot(2)` command |
| --- | --- | --- |
| `halt` | `SIGUSR1` | `RB_HALT_SYSTEM` (`0xcdef0123`) |
| `poweroff` | `SIGUSR2` | `RB_POWER_OFF` (`0x4321fedc`) |
| `reboot` | `SIGTERM` | `RB_AUTOBOOT` (`0x01234567`) |

BusyBox PID1 first executes its original `/etc/inittab` shutdown actions,
then issues the final `reboot(2)` **from a vfork child**. Critically, original
BusyBox's `pause_and_low_level_reboot()` puts the parent PID1 in an
**infinite sleep loop** after spawning this child; PID1 does *not* exit.
Thus the syscall must neither require caller PID1 nor unmount ext4 or invoke
a host debug-exit from the child's syscall stack.

## Implementation and safety boundary

1. The common signal core permits PID1 to receive only explicitly blocked
   or caught signals. Default-fatal and unmaskable signals remain protected.
2. The common syscall decoder maps x86-64 `__NR_reboot=169` and both RV64
   and AArch64 `__NR_reboot=142` to one internal operation.
3. Dispatch validates the Linux reboot magic pair and command, and records
   the shutdown signal only AFTER the original PID1 consumed it in
   `sigtimedwait` (including blocked syscall completion). The final request
   must come from PID1's still-sharing-VM `vfork` child, not an unrelated
   service or a shell process merely reparented to PID1.
4. `RB_HALT_SYSTEM` and `RB_POWER_OFF` **never return to the BusyBox child**.
   A valid syscall first retires the vfork child through the established
   `exit_group` path, which releases the shared-VM vfork. On the independent
   native scheduler stack, after the child is gone, the boot policy retires
   BusyBox's indefinitely sleeping PID1 through its real `exit_group`/FD
   cleanup. Only then may the **existing** deferred task quiescence,
   asynchronous worker drain, actual ext4 unmount and durable flush proceed.
   Original OpenRC also creates runtime tmpfs mounts (e.g. `/run`). The
   final shutdown first detaches *all* runtime tmpfs superblocks through
   ordinary VFS unmount and inode teardown, deepest first, **before** the
   built-in pseudo mounts and physical ext4 root. Never force-zero a live
   mount reference to bypass `EBUSY`.
5. A missing final request fails PID1 completion instead of printing a
   fraudulent normal-shutdown PASS. Direct `poweroff -f` from unrelated
   processes is rejected instead of reporting a false success.
6. **`RB_AUTOBOOT` is not implemented.** It returns `-EOPNOTSUPP`, and an
   attempted BusyBox shutdown without a supported final request cannot
   satisfy the completed-shutdown marker. A real platform reset, separately
   tested after an on-disk state change, is required for a reboot PASS.

The existing platform debug-exit/halt is a *QEMU test completion* mechanism;
it is not yet a physical FPGA board's power or reset controller.

## Fast local proof (does not boot QEMU)

```sh
sh scripts/check-m241-original-alpine-shutdown.sh
```

These execute production signal routing, the immediate and blocked
`sigtimedwait` dispatch paths, and PID1 shutdown functions with deterministic
host Task fixtures, including armed and unarmed M180 workers, but they are
**not** a native original-Alpine shutdown result.

The isolated `na-alpine-direct-use` feature branch has an automatic
seconds-scale host workflow. Changes in this branch deliberately do not
start the long M240 three-ISA/SSH-PTY proof. After native M241 acceptance,
one consolidated merge to `naseed-app-batch2` will trigger that regression.

An independent `m241-original-alpine-poweroff-x86.yml` push workflow runs
the first native proof automatically on this feature branch. It stages one
verified original Alpine disk and runs **one** x86-64 QEMU, reusing the M193
Limine boot profile but never running the prior M191 baseline QEMU first.

First native run `42564799325` did execute genuine OpenRC poweroff and
reach PID1 quiescence. It correctly **failed** at ext4 root unmount with
`open_refs=2 child_refs=1`, identifying the one remaining OpenRC-created
runtime tmpfs attachment. This was a failure receipt, not a native PASS.

The fixed **fourth** native run `42566040745` (`863b3131`) now **passes**:
real original x86-64 Alpine getty/password and OpenRC, guest ext4 write,
guest readback and sync, original `/sbin/poweroff`, PID1 quiescence, final
upstream ext4 unmount/flush, QEMU natural process exit, disk lock release,
read-only host e2fsck and independent physical `debugfs` nonce readback.
Native CI emitted `[M241 ORIGINAL ALPINE POWER OFF NATIVE PASS]`. Its
raw serial/runner artifact is `13651178821` (retained seven days).

The two other ISA runners previously stopped QEMU **from the host** after
observing a debug marker, whereas x86 used an actual exit device. M241
has a separate opt-in machine-power path instead of modifying the generic
parity completion semantics: on RV64 it requests OpenSBI SBI SRST
shutdown, and on AArch64 the QEMU `virt` HVC conduit requests PSCI
SYSTEM_OFF. Only a fully validated original BusyBox POWER_OFF state can
reach that platform action, after ext4 teardown. The M241-only runner
awaits a genuine QEMU process exit; it must not host-kill QEMU for PASS.
These changes require independent remote native acceptance on both ISAs.

Run `42567126109` independently **passed RV64** (artifact `13652011615`):
authentic Alpine login, nonce write/guest readback, original poweroff,
PID1 quiescence, OpenSBI SBI SRST naturally terminating QEMU, clean
physical ext4 and host nonce readback. The first AArch64 trial on the
same run stopped **before QEMU guest boot**, due to absent packaged
`efi-virtio.rom` in the CI runner. The workflow now installs
`ipxe-qemu`, as the preexisting M193 cross-ISA workflow did; AArch64
native shutdown remains UNPROVEN until a new actual guest run passes.

The automatically push-triggered `m241-original-alpine-poweroff-ports.yml`
executes one QEMU boot per RV64/AArch64 ISA and does not replay the prior
M191 QEMU baseline or M240 SSH matrix. A green host preflight is not proof
of either native platform shutdown.

## Native acceptance still required

Start one independent original signed Alpine root disk (no replacement
`/sbin/init` or test-provided reboot implementation). On the guest:

1. Write a nonce to physical ext4 and `sync`.
2. Run original `/sbin/poweroff` on the serial login (not QMP quit).
3. Preserve the UART evidence for original OpenRC shutdown actions,
   `ALPINE POWER_OFF`, deferred process quiescence and clean ext4 unmount.
4. The QEMU process must end via the expected platform test completion,
   not a forced timeout. After the QEMU image lock has been released,
   check physical ext4 with read-only `e2fsck` and `debugfs` the original
   guest-written nonce. A second *independent* QEMU boot is a later
   cross-boot persistence regression, not a prerequisite for this fast pilot.
5. After the first native ISA passes, repeat unchanged on AArch64 and RV64.

Do not report a three-ISA M241 PASS until all three native proofs exist.
Keep the M240 original SSH PTY proof immutable and avoid rerunning all
historic cohorts for each small kernel change.
