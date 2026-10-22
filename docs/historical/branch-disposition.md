# Historical branch dispositions

NaSeed's permanent development ref is `main`. These short-lived work branches
were audited and retired after their corresponding functional stage was replaced
by later merged mainline implementations. The safety workflow verifies each
branch's immutable pre-audited HEAD SHA and checks no open PR before deletion.

- `design/arch-contract-v1`: design-only contract preserved in this folder.
- `feature/vfs-open-file-ops-naos-alignment`: closed unmerged PR #70 was
  superseded by merged typed-file-ops PR #71; early review notes preserved.
- `port/m96-aarch64-el0-svc`: superseded by the later AArch64 shared-kernel
  real user EL0/ELF/BusyBox acceptance.
- `staging/m28-rv64-arm64-rebase`: staging branch superseded by PR #69
  and the merged three-ISA real timer-preemption feature.
- `work/m30-batched-signal-lifecycle`: abandoned intermediate design,
  superseded by PR #73's merged Linux-native signal lifecycle.
- `work/m30-native-frame-backends`: shares the exact head commit SHA of
  the merged PR #73, so the completed code already exists on `main`.

No obsolete ref is deleted if its head has moved since the audit.
