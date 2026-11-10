# M202 — real Linux VT with unmodified Alpine `fbi` hardware input

## Architectural advance

M201 proved original, signed Alpine 3.23 `fbida-fbi` 2.14-r6 opens
`/dev/fb0`, maps the real NaSeed VirtIO-GPU framebuffer, displays one
host-supplied PNG, and exits 0 on all three native QEMU ISAs. It deliberately
specified `--nointeractive` and did **not** prove Linux virtual console
ownership or keyboard input to the original application.

M202's one cohesive target is original Linux virtual-console operation:

- A genuine `/dev/tty1` independent of Alpine's existing UART `ttyS0`, with
  Linux raw termios and virtual terminal/keyboard display ownership.
- Reuse the existing QEMU VirtIO-Input IRQ, input core and evdev devices,
  routing keyboard input into the active console. No duplicate hardware driver,
  host UART key injection or replacement guest application.
- Original Alpine `/sbin/init`/OpenRC/getty/login/ash executes **the exact
  signed APK `/usr/bin/fbi`**, with `--interactive`, genuine child-exit wait and stdin
  bound to `/dev/tty1` in a new session. BusyBox `setsid` may fork and return
  0 before the child launches; the real child writes its eventual fbi exit
  status to ext4, which the authenticated Alpine ash waits for. Its `kbd.c` default stdin path must
  consume the physical keyboard; opt-in libinput/udev is not presumed working.
- Upstream `vt.c` uses Linux VT_GETMODE/SETMODE, KDGETMODE/SETMODE and
  VT_GETSTATE/ACTIVATE/WAITACTIVE; `kbd.c` uses TCGETS/TCSETS and poll/select.
  The shell must still run after the original fbi quits. A missing VT/KD
  implementation cannot silently count as successful ownership.

## Independent physical acceptance

The host stages the **same original signed M201 Alpine installation**, its
verified APK DB, original `fbida-fbi` ELF and `font-dejavu` without re-fetching
a new software cohort in the guest. It adds exactly one second **input PNG**
to a copy of the physical ext4 disk. Both original PNG input fixtures are
CRC-correct, 800×600 RGB, and bit-for-bit distinct. The second image is
inserted via host `debugfs`, read back exactly, and checked with `e2fsck`.

One native system-QEMU per ISA must satisfy all of the following:

1. Original Alpine PID1, OpenRC, authenticated getty/login/ash, original signed
   APK database, and actual `/usr/bin/fbi` ELF execution from ext4.
2. Independent host QMP `screendump` records image 1 on the real 800×600 GPU:
   exact pixel samples `(100,300)` red, `(300,300)` green, `(500,300)` blue,
   `(700,300)` white. Guest stdout cannot substitute.
3. Host QMP injects *real* VirtIO keyboard Space down/up; a second physical
   GPU `screendump` must now contain `(100,300)` cyan, `(300,300)` magenta,
   `(500,300)` yellow, `(700,300)` black. An unchanged first screen fails.
4. Host QMP injects *real* VirtIO keyboard `q` down/up. Only the genuine
   original Alpine fbi child returning `M202_FBI_EXEC_RC=0` after its new-session
   shell writes the actual process exit status qualifies; setsid's parent's
   earlier exit 0 cannot satisfy this criterion.
5. The Alpine shell checks that upstream did not report failed Linux VT
   switching and the real `/dev/tty1` has working termios after fbi quits.

The QMP parser requires distinct entire UART output lines; the original
Alpine native serial stream may use LF, CRLF or CRCRLF. It never accepts
an echoed `printf` command as execution proof. A host-only success file
coordinates the original shell's final check **after** QMP finished. QMP
images, real UART, real signed ELF hash and original APK DB are saved as CI
artifacts.

Run the strict native acceptance:

```sh
sh scripts/probe-m202-alpine-interactive-fbi.sh amd64
sh scripts/probe-m202-alpine-interactive-fbi.sh riscv64
sh scripts/probe-m202-alpine-interactive-fbi.sh arm64
```

The targeted workflow is `.github/workflows/m202-original-alpine-interactive-fbi.yml`.
No M197 public APK WAN transaction, M198 double-reboot GUI/Vim, M191 warm-up
QEMU, or whole historical CI gate is part of this focused proof. Host contracts
are only preparation; final M202 PASS requires all three exact-head native
physical system-QEMU jobs to actually pass.
