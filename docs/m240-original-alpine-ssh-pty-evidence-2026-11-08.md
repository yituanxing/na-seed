# M240 original signed Alpine Dropbear SSH-PTY evidence (2026-11-08)

This records real native guest evidence separately from static/host tests. The
M240 release oracle requires **two independent real QEMU boots, nine host
OpenSSH `-tt` PTYs per boot, full terminal interaction, separate NIC PCAPs,
and the original signed Alpine physical ext4/OpenRC chain**. Neither a green
workflow nor an SSH TCP connection alone is a substitute for this proof.

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
