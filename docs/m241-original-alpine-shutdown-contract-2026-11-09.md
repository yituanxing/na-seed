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
then issues the final `reboot(2)` **from a vfork child**, waits for that child,
and exits itself. Thus the syscall must neither require caller PID1 nor
unmount ext4 or invoke a host debug-exit from the child's syscall stack.

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
4. `RB_HALT_SYSTEM` and `RB_POWER_OFF` return to the BusyBox child; only
   PID1's final exit initiates the **existing** deferred task quiescence,
   asynchronous worker drain, actual ext4 unmount and durable disk flush.
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

## Native acceptance still required

Start one independent original signed Alpine root disk (no replacement
`/sbin/init` or test-provided reboot implementation). On the guest:

1. Write a nonce to physical ext4 and `sync`.
2. Run original `/sbin/poweroff` on the serial login (not QMP quit).
3. Preserve the UART evidence for original OpenRC shutdown actions,
   `ALPINE POWER_OFF`, deferred process quiescence and clean ext4 unmount.
4. The QEMU process must end via the expected platform test completion,
   not a forced timeout. Check the physical ext4 with read-only `e2fsck`
   and reopen it in a second *independent* QEMU; verify the nonce persists.
5. After the first native ISA passes, repeat unchanged on AArch64 and RV64.

Do not report a three-ISA M241 PASS until all three native proofs exist.
Keep the M240 original SSH PTY proof immutable and avoid rerunning all
historic cohorts for each small kernel change.
