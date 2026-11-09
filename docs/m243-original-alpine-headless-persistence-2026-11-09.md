# M243: upstream Alpine CLI software + OpenRC across one real QEMU reboot

Scope: official, originally signed Alpine 3.23.7 command-line rootfs, **no
desktop**, on native NaSeed x86-64, RV64 and AArch64 ports. The OS is tested
with upstream Alpine programs, not a synthetic shell/rc/apk replacement.

## The concrete direct-use contract

1. Boot the signed Alpine root, use its original OpenRC PID1/getty, authenticate
   in its original ash over the guest UART, and obtain DNS from **real DHCP**
   rather than a host-injected static resolver.
2. Prove `tree` and `jq` absent from the pristine baseline; run actual upstream
   `apk --no-cache add tree jq` over guest VirtIO/lwIP HTTPS with the original
   signed repositories and Alpine trust material. Verify the installed package
   DB and execute both new original native ELF programs.
3. Through upstream `rc-update` and `rc-service`, enable, launch and inspect
   the original Alpine `crond`; persist a unique witness file to ext4 and sync.
4. Use original `/sbin/reboot`, with the actual platform reset happening in
   the **same living QEMU process**. Reenter original OpenRC/getty/password
   login. Check both APK records and ELFs again; require `crond` to be running
   **automatically**, without a second explicit `rc-service start`; read the
   first-boot witness.
5. Use original `/sbin/poweroff`, require natural QEMU exit, then independently
   extract the disposable disk's real ext4 partition, fsck and read its APK
   world/installed database, native ELF ISA, original base metadata, exact
   witness, and guest-created default-runlevel symlink to `crond`.

The immutable signed Alpine baseline is checked separately and must lack the
new `crond` runlevel link. PCAP, native UART transcript and runner log are
retained with the CI job. No desktop stack, browser, framebuffer or synthetic
service launcher participates in this gate.

## Evidence and adjudication

- [Initial full native run 42571584223](https://github.com/yituanxing/na-seed/actions/runs/42571584223): all three actual QEMU guests completed the two-boot
  workload, including public `apk`, `crond`, same-process firmware reset,
  second login and final natural poweroff. This run is **not** an overall PASS:
  the post-exit host verifier was run as a nonroot user and could not traverse
  the signed baseline's mode-0700 `/root` directory.
- The host-only permission issue was fixed by invoking the independent
  physical disk checker with `sudo`, as previous M241 baseline checks already
  did. No kernel, guest package, network or UART oracle semantics were changed.
- [Retry on the fixed verifier, run 42572413373](https://github.com/yituanxing/na-seed/actions/runs/42572413373): this is the authoritative full gate; only
  mark M243 complete if **all three** jobs pass the post-QEMU fsck/APK/OpenRC
  disk checks as well as the native guest.

Host fast tests: `scripts/test-m243-alpine-cli-uart.py` (16 cases), and the
existing M242 two-epoch contract (11 cases). End-to-end entrypoint:
`scripts/probe-m243-alpine-cli.sh`; CI:
`.github/workflows/m243-original-alpine-cli-persistence.yml` (automatic on
relevant feature-branch pushes, **not** a manually dispatched green job).

## Next direct-use boundary: M244

Build on this same-process reboot contract and previously demonstrated
upstream Dropbear/OpenRC + host OpenSSH PTY checks. Require live host SSH
login, genuine original `/bin/ash -i` PTY interactions, guest SSH phase
acknowledgments and stable host key before **and after the same QEMU reset**;
keep original packages and OpenRC service state, verify after final poweroff.
Reuse the M230/M240 SSH oracles but avoid their slow historical full 9×2
session suite in the fast gate. Continue CLI-only; no desktop work.
