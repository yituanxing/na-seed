# M244: native original Alpine command-line SSH across one genuine QEMU reset

This is the **no-desktop** direct-use track of NaSeed, building on the
[M243 original signed Alpine apk/OpenRC three-ISA physical gate](m243-original-alpine-headless-persistence-2026-11-09.md).
It reuses upstream signed Alpine and existing NaSeed M230/M240 guest/host
Dropbear/PTy work. It does not implement its own sshd, init, package manager
or desktop.

## End-to-end gate

- Signed upstream Alpine 3.23.7 original OpenRC/getty/root login in native
  x86-64/RV64/AArch64 QEMU; original public HTTPS repo, APK trust and live
  DHCP DNS. Prove `tree`, `jq`, `dropbear` and `dropbear-openrc` installed as
  original signed APK packages, exercise original ELF binaries.
- The original Alpine ifupdown-ng/`networking` OpenRC dependency uses an
  `eth0` declaration and a real DHCP/DNS interface readiness pre-up hook;
  preserve the original `networking` init script. Original `rc-update add
  networking boot`, `rc-service networking start` precede the original
  Dropbear `rc-service start`.
- A disposable host-only Ed25519 client key is generated outside guest and
  NEVER uploaded. Guest receives only its public key and creates restricted
  `/root/.ssh/authorized_keys`. Original Dropbear owns its own persistent
  Ed25519 server identity and default OpenRC runlevel link.
- An independent host OpenSSH client connects through QEMU's **single**
  VirtIO-Net NIC (loopback `127.0.0.1:12222` to guest `15446`), verifies a
  genuinely interactive original Alpine `/bin/ash -i` terminal (`ssh -tt`),
  terminal size, canonical DEL/backspace and signal-generated Ctrl-C.
  Only after real PTY PASS is the guest's first-epoch ACK written via a
  separate authenticated SSH remote command.
- The genuine original Alpine `/sbin/reboot` must reset firmware **within
  the same QEMU PID**, not execute a second process, reuse a prior UART log,
  or swap an image. The original second OpenRC boot auto-recovers both
  networking and Dropbear, *without manual service start*. The host must
  connect, authenticate and repeat the real PTY interaction, matching its
  previously pinned server host key; only then does its second guest ACK
  release original `/sbin/poweroff`.
- After natural QEMU exit, an independent read-only host verifier checks
  physical ext4 fsck, persisted M243 apk/crond/nonce, Dropbear APK database,
  native ELF ISA, guest-created networking and dropbear OpenRC links,
  authorized key policy, server private-key permissions and SHA, two guest
  SSH phase receipts, two independently nonce-bound host JSON receipts,
  and at least two real encrypted guest SSH TCP sessions in the NIC PCAP.

This is an isolated **root-only functional** SSH gate, NOT evidence of
production multi-user security: full non-root DAC semantics are still
outside the scope of the NaSeed kernel.

## Regressions and actual source runs

- M242 genuine three-ISA same-process reboot and M243 real signed Alpine
  software/cron physical checks were **3/3 PASS** before this gate; these
  retain their separate CI owners.
- [M244 initial native run #42576121224](https://github.com/yituanxing/na-seed/actions/runs/42576121224): all three real guests rejected
  `rc-service dropbear start` because its original `networking` dependency
  had no parseable `/etc/network/interfaces`; UART showed
  `ifquery: could not parse /etc/network/interfaces` and genuine OpenRC
  refusal. This was a missing M227 networking initialization migration,
  NOT a Dropbear or kernel substitute bug. No SSH/PTY PASS was claimed.
- [M244 networking-fix native run #42577314661](https://github.com/yituanxing/na-seed/actions/runs/42577314661): all **three** real guests installed genuine
  signed Dropbear, started original OpenRC networking/Dropbear and achieved
  the **first** authenticated host OpenSSH PTY PASS. The UART owner then
  stopped in `await_prompt step=25` after the real line
  `localhost:~# ESC[6n[PIPE] created read=6 write=7` because its accepted
  asynchronous printk shapes missed this exact kernel message. This was
  a verification-controller race, **not** an SSH or kernel execution failure;
  no second-epoch or complete acceptance was claimed.
- [M244 final native run #42579646858](https://github.com/yituanxing/na-seed/actions/runs/42579646858), commit
  `87b52140f771352501997a7cf4931ed63ac10078`: **3/3 FULL PASS**
  (`amd64`, `riscv64`, `arm64`). Strictly recognizing the real asynchronous
  `[PIPE] created read=N write=N` suffix to a fresh ash prompt (plus a
  forged-suffix negative test) restored UART progress without loosening the
  real terminal, SSH authentication, same-QEMU PID, ext4 or network gates.
  Each native job emitted **all** of:

  - `[M244 ORIGINAL ALPINE TWO-EPOCH HOST SSH PASS]` after two real host
    OpenSSH PTYs and the genuine original Alpine reboot/poweroff lifecycle;
  - `[M244 ORIGINAL ALPINE SSH HOST+WIRE+DISK PASS]` after independent
    physical ext4, persistent original APK/OpenRC/SSH hostkey and encrypted
    guest NIC proof;
  - `[M244 ORIGINAL ALPINE SSH PTY SAME QEMU NATIVE PASS]` for its ISA;
  - inherited `[M243 ORIGINAL ALPINE CLI NATIVE PASS]` and
    `[M242 ORIGINAL ALPINE TWO BOOTS PASS]`.

  GitHub Actions preserved expiring seven-day genuine UART/host-SSH/PCAP
  evidence artifacts, one per ISA:
  `m244-original-alpine-same-qemu-host-ssh-pty-amd64` (#13656962256),
  `m244-original-alpine-same-qemu-host-ssh-pty-riscv64` (#13656593192),
  `m244-original-alpine-same-qemu-host-ssh-pty-arm64` (#13656863516).

Targeted fast host tests: 23 UART cases, 9 real-host-watcher contracts,
12 independent disk-receipt cases and the real M194 printk/ash prompt
regression, plus inherited M243/M242 tests. Focused CI:
`.github/workflows/m244-original-alpine-ssh-pty-reboot.yml`, automatic
feature-branch push trigger. Historic M240 9x2 stress sessions remain a
separate slow专项 gate and are not replayed on every fast iteration.
