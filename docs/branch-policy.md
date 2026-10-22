# NaSeed branch and CI discipline

Keep `main` as the only long-lived development branch.

1. Select one interesting real-world program and a coherent functionality slice.
2. Make one short-lived PR branch; independently compare the original Linux
   binary under Linux/QEMU-user, then the unmodified program on NaSeed.
3. Use cheap static/host checks first. Run focused guest acceptance for the
   new program; run the established BusyBox three-ISA regressions when shared
   kernel, loaders, drivers, or boot fixtures change.
4. Merge promptly **only after the required tests actually pass**; never
   present a cancelled, queued, or partially green workflow as acceptance.
5. Automatically delete merged PR branches, but only when GitHub confirms
   a same-repository merged PR and the branch tip still equals its exact
   merged head SHA. Never delete a branch with an open PR or unverified work.
6. Avoid parallel long-running `work/` or `staging/` branches. If such a
   branch carries unique work, first review and consolidate it.

Failures in secondary features may be documented while moving on to other
interesting programs. Mainline regressions and Linux ABI correctness must
never be disguised as successes.
