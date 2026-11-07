# M199 — original Alpine OpenRC owns NaSeed's graphical session

Status: host-only implementation and source contracts PASS; real three-ISA
QEMU/OpenRC runtime acceptance NOT YET RUN. M198's original Vim frame oracle
currently fails in three ISAs even though its ELF successfully execs; M198
must collect the actual Vim screenshot and delta statistics first.

## Why this milestone

M197 verified genuine external Alpine APK, Bash/Lua/SQLite/Git and persistent
ext4; M198 brings the existing VirtIO-GPU/evdev/PTY desktop into that same
original Alpine userspace. M199 must then let the **distribution's** original
`/sbin/init` and OpenRC start, stop, restart and restore the graphical session.
No custom init, no CPIO desktop stub, no new GUI stack or duplicate driver.

## Ownership and data flow

* The immutable Alpine 3.23 signed root and existing M191 kernel are unchanged.
* Host stages M198's existing GUI applications and one ordinary third-party
  `/etc/init.d/naseed-desktop` script in a **disposable** ext4 root overlay.
* Crucially, host staging does **not** install a default-runlevel symlink.
* On first native QEMU boot, original `getty → login → ash` runs M197's real
  signed public HTTPS workload and calls original `rc-update add` and
  `rc-service` on the graphical service. `naseed-desktop m187` is never
  hand-started by the serial controller.
* Original OpenRC owns its pidfile and logs. SIGTERM uses the compositor's
  normal `MSG_QUIT → wait4 → mmap/FD/socket cleanup` path. An abandoned UNIX
  socket is reclaimed only if it is safely identified as stale.
* An independent QMP process generates real tablet/keyboard events, observes
  real VirtIO GPU pixels and uses original `/usr/bin/vim` via the live PTY.
  The host success file only gates *when* OpenRC may stop the service, never
  substitutes for an actual graphics or guest result.
* The first QEMU process closes and releases the real ext4 image. A second
  **original Alpine `/sbin/init`** boot uses the same disk and guest-created
  `default/naseed-desktop` link to start the GUI automatically. Its serial
  test intentionally does not invoke `rc-service ... start`.
* The second QMP test checks fresh framebuffer and real keyboard pixels;
  original Alpine Bash checks previous Vim and M197 software data, and the
  host independently reads ext4 via `debugfs` and verifies `e2fsck`.

## Why this is not M198's old second boot

M198's persistence checker starts a CPIO BusyBox one-shot command with Alpine
ELFs from ext4. That proves data can be reopened but **not** that original
Alpine PID1 and OpenRC came up again. M199 explicitly sets
`scripts/m199-alpine-boot-cases.tsv` to original `/sbin/init` for both QEMUs.

## Strict acceptance matrix

| Stage | amd64 | riscv64 | arm64 |
| --- | --- | --- | --- |
| Host source/OpenRC contracts | PASS | PASS | PASS |
| First genuine Alpine PID1 + public signed APK | pending M199 CI | pending | pending |
| Guest `rc-update` and full service lifecycle | pending | pending | pending |
| First QMP physical original Vim/ELF/screen | pending (M198 oracle red) | pending | pending |
| Second genuine Alpine PID1 auto-start + QMP | pending | pending | pending |
| Independent host runlevel/link/Vim/ext4/fsck | pending | pending | pending |

The expensive three-ISA M199 workflow should be enabled and triggered only
after the prior M198 screenshot/threshold diagnosis has a supported resolution.
Never label source checks as system-QEMU success, and never silently skip a
port when collecting final runtime evidence.
