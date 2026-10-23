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

## Headless CI package policy

Use `apt-get install --no-install-recommends` for x86, RV64 and ARM64
headless CI runners. A slow mirror previously transferred 48.3 MB and
installed about 95 unrelated QEMU GUI/Spice dependencies in a focused
build; the headless-only rerun completed in under one minute. All existing
native BusyBox and seven regression shards remain enabled; do not
silently trade away verification for faster apparent green status.

When editing userland-only tests, use a focused workflow rather than
adding app-specific kernel shortcuts. If the shared kernel changes, run
the multi-ISA baseline, and merge only the verified coherent result.

## Separate projects, separate CI designs

NaSeed's existing jobs are independent kernel/real-software regression gates.
They are **not** MiniC's seven-part Linux build/verification strategy,
and NaSeed has no requirement to force exactly seven shards. Optimize
only when real Actions timings identify waste (package downloads, repeated
builds, unnecessary CI triggers), without reducing relevant verification.

AArch64's UEFI boot tests have no network interface. Keep the existing
virtio-block boot device but run QEMU with `-nic none`, so missing
optional iPXE `efi-virtio.rom` network ROMs cannot make a headless
firmware test fail after `--no-install-recommends`. QEMU must still
execute the same real PTE/ELF/system tests.
