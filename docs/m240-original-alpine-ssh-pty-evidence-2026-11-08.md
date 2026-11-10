# M240 original signed Alpine Dropbear SSH-PTY evidence (2026-11-08)

This records real native guest evidence separately from static/host tests. The
M240 release oracle requires **two independent real QEMU boots, nine host
OpenSSH `-tt` PTYs per boot, full terminal interaction, separate NIC PCAPs,
and the original signed Alpine physical ext4/OpenRC chain**. Neither a green
workflow nor an SSH TCP connection alone is a substitute for this proof.

## 2026-11-09: full genuine x86-64 M240 9x2 PASS

- [GitHub Actions run #42551821112](https://github.com/yituanxing/na-seed/actions/runs/42551821112),
  job `128948861670`, head
  [`1b75132`](https://github.com/yituanxing/na-seed/commit/1b7513226eb64cfbe150be0cfa8e275ce6a2a12d):
  **completed/success**. Native QEMU step 6 and evidence-upload step 7 each
  succeeded. The actual Job log explicitly reports
  `[M240 ORIGINAL ALPINE SSH PTY TWO BOOT PASS]` and
  `[M240 ORIGINAL ALPINE DROPBEAR HOST SSH PTY TWO BOOT PASS]`.
- [Original artifact #13645824939](https://github.com/yituanxing/na-seed/actions/runs/42551821112/artifacts/13645824939),
  ZIP SHA-256
  `690ffa338e6e56f2541ae34e9f7637dfc4e3e356a36d427d023ed3b1188667dd`.
  The two independent native guest boot receipts are preserved as:
  - `m240-first-host-ssh-pty.json` SHA-256
    `35749f0d280b6cf32b9f51fa2e126e01e5d3e6261043c3f362935e87677e1d1f`;
  - `m240-reopen-host-ssh-pty.json` SHA-256
    `4e563f17a7b888142dc930809916642c8da1cb7c0b73279a7fbba72c974af749`.
- **Each** of these original host OpenSSH `-tt` receipts has
  `session_count=9`, independent nonce challenges, and nine successful
  `tty_allocated`, `canonical_backspace`, `foreground_sigint` and
  `winsize_33x101` assertions. Across the eighteen sessions, all nonce
  digests are distinct, and each real BusyBox ash answered one received
  VT100 CSI 6n cursor query through the repaired headless host terminal.
  The pinned Dropbear Ed25519 host key fingerprint is identical across
  actual QEMU boots:
  `SHA256:+eAhFXi4zYrAOqeB8fq698/WnQbFSMJZ6d4wNrlIMHM`.
- The independent *guest eth0* NIC capture pair in the final-oracle JSON
  records **nine actual encrypted SSH TCP connections per boot**:
  - first `m197-public-wire.pcap` SHA-256
    `97e05b60abe4ac4e2fee73f29fd84dc0527c75b39edc90d2e3cc5a190749709d`;
  - reopen `m240-reopen-wire.pcap` SHA-256
    `f119aaf7d5e1d8a633c6dfcbde046ac6cf7e6534d19bedc0031790c07274ea3b`.
  Both SHA-256 hashes were independently checked against the captured PCAP
  bytes. Both UARTs include genuine original Alpine PID1/OpenRC, original
  `/usr/sbin/dropbear`, successful host SSH challenge windows, script RC=0,
  and successful physical ext4 sync/reopen checks. The immutable original
  signed Alpine package database, persisted Ed25519 host key, and read-only
  host ext4 fsck are verified independently.
- The artifact expires at `2026-11-10T02:55:43Z` according to GitHub's
  metadata; this section retains hashes and the acceptance result even
  when the downloadable ZIP expires. Credential-bearing guest disk images
  and private keys were **not** uploaded. This is a disposable,
  root-only *functional* validation, **not production security/DAC approval**.

## 2026-11-09: genuine 9/9 first boot and 7/9 second boot; still FAIL

- [Real M240 x86-64 QEMU run #42549065918](https://github.com/yituanxing/na-seed/actions/runs/42549065918),
  kernel accept-lease repair [`a91d4b7`](https://github.com/yituanxing/na-seed/commit/a91d4b7a6673a06a3569f9e40c7e997b057b5a97):
  **the first independent boot completed all nine actual host OpenSSH `-tt`
  sessions** with canonical DEL, PTY winsize, genuine foreground SIGINT and
  shell exit. The second original signed Alpine/OpenRC/Dropbear boot completed
  **seven** such sessions; host session 8/9 timed out. The overall required
  **9x2 oracle remains FAIL**. QEMU run conclusion was failure; do not turn
  9/9 + 7/9 into a complete pass.
- Artifact `13644949287`, original GitHub ZIP SHA-256
  `965a8e392f2a95aa15d3b475f9d761d71d876bac55792a86fb4bff30664df70a`:
  first boot host JSON receipt, both original UART logs, host SSH logs and
  distinct boot NIC PCAPs. The second UART includes genuine native lwIP
  `[LWIP ACCEPT PARKED]` and `[LWIP ACCEPT REPLAYED]` records, directly
  exercising the repaired FD-lease lifecycle. The repair also has an
  independent native Netconn final-listener-close regression in `c2aa0df`.
- The eighth second-boot SSH session successfully reached original guest
  Dropbear and interactive BusyBox `ash`; host stdout ended with the actual
  `~ # \x1b[6n` cursor-position request. The host currently pipes OpenSSH's
  forced remote PTY to non-terminal file descriptors, with no ANSI terminal
  emulator to answer CSI 6n. This suggests **host terminal emulation** as
  the next concrete blocker, not an observed TCP RST. The updated host
  checker replies to CSI 6n **only when actually received** and preserves
  all original PTY acceptance assertions; native 9x2 revalidation is pending.

## Two x86-64 pilots: real failure, not M240 PASS

1. [Initial run #42495058653](https://github.com/yituanxing/na-seed/actions/runs/42495058653),
   head `b5a959b855e00b85465f203d5f94542610b3d8ac`: original signed Alpine
   Dropbear/OpenRC reached first UART readiness and a genuine host OpenSSH
   TCP session allocated a PTY and executed original `ash`, `stty`, and `tty`.
   The old strict host proof parser reported `remote proof missing`.
   **FAIL**. Artifact `13623018461`, ZIP SHA-256
   `53dafe7369e2892c89e5327d4db42f5f44b5ebc0655f582247b49a62b5f3605c`.
2. [Corrected run #42495826836](https://github.com/yituanxing/na-seed/actions/runs/42495826836),
   head `61a8f5301efd6e2fecec59a80014257839840437`: first original
   Alpine guest QEMU reached signed Dropbear/OpenRC. **Host SSH-PTY sessions
   1/9 and 2/9 each fully passed** path `/dev/pts/N`, two real terminal FDs,
   `stty` 33x101, canonical DEL, foreground `/bin/sleep` terminated by genuine
   kernel SIGINT, and graceful exit. The third host connection failed before
   SSH banner exchange or guest PTY allocation. Thus the full **9x2 M240
   oracle remains FAIL**, not 2/9 interpreted as complete success.
   Artifact `13623852318`, ZIP SHA-256
   `6a16112a2c73490889b0dbcab0013fba137f8c3f937a5efded4b015e5fbda90f`.

The second run's **actual guest Ethernet PCAP** (not serial-only markers)
provides the following TCP timeline in UTC:

| Host TCP source | First SYN | Real guest outcome |
| --- | --- | --- |
| `51662` | `15:58:39.373` | Full SSH PTY session, closed normally |
| `55306` | `15:58:43.317` | Full SSH PTY session, closed normally |
| `55320` | `15:58:47.387` | SYN/SYN-ACK, 43-byte OpenSSH identification; guest ACK then **guest RST+ACK at 15:58:47.540**. No Dropbear server identification, no third guest Dropbear fork. |

The third failure was roughly 153 ms after SYN. It does **not** establish
an exhausted 300-second guest window, PTY slot exhaustion, or a SIGWINCH/
Ctrl-C fault. Actual cause remains unresolved between original Dropbear
admission/accept and the native lwIP Netconn lifecycle. The Dropbear parent
reaped both completed children; there was no demonstrated zombie buildup.

## Changes introduced for the next decisive run

- [`af9ac64`](https://github.com/yituanxing/na-seed/commit/af9ac64ad24a0d0d7c1212eb235a8a034cccc80a):
  bounded host OpenSSH stdout/stderr and exit diagnostics; original guest
  Dropbear syslog in UART; rare native lwIP accept-error trace (status,
  pending, active), plus SIGWINCH default/caught/masked signal support and
  its host group-signal regression.
- [`fdbe981`](https://github.com/yituanxing/na-seed/commit/fdbe981b4881e13ebf23e247647b21fcec831ffb):
  make the native accept-error decimal trace linkable with independent lwIP
  host fixtures. No pool/timeout expansion or service replacement.
- [Next M240 x86-64 native run #42499099150](https://github.com/yituanxing/na-seed/actions/runs/42499099150)
  started at head `af9ac64`. Its outcome must be audited from new original
  logs and artifacts before any assertion of M240 success.

Local tests: `scripts/test-m240-ssh-pty-contract.py`,
`scripts/check-m105-signal-group.sh`, `scripts/check-lwip-accept-syscall.sh`
passed. The full local `scripts/check-lwip-foundation.sh` could not run in
this scratch checkout because the upstream lwIP submodule is absent; native
CI must build the actual Netconn source before any runtime claim.

## Preserved original M230 no-PTY baseline

The unrelated M230 restrictive-key baseline is **3/3 real native QEMU
PASS**, not merely green CI: [run #42495058532](https://github.com/yituanxing/na-seed/actions/runs/42495058532).
Each ISA's raw Job log contains first and reboot OpenRC/UART and host SSH
receipts plus independent NIC capture and physical ext4/read-only fsck.

| Native ISA | Artifact ID | GitHub ZIP SHA-256 |
| --- | --- | --- |
| x86-64 | `13624025736` | `6284ef7e767acc60d4a66309adf7a0da25a0d99b91bc88b09959c7a595dd678f` |
| RV64 | `13623950950` | `87c80df1a4e493cfc5b0e8f57eca59e4198cf2022285b67a41d60a1851c97b03` |
| AArch64 | `13624200772` | `0566516fda1096981bd554ea2c521da5a1144d6d48d978794af1170ad8c6826d` |

M230 uses `restrict` (no PTY), distinct from M240's explicitly allowed-PTY
test key. Neither baseline can substitute for the other's security or
functional oracle. All credential-bearing ext4 disks remain unuploaded.
