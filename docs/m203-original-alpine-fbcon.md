# M203 — real Linux framebuffer text console and original Alpine VT login

M202 established the native Linux `/dev/tty1` VT/KD/termios API and genuine
VirtIO keyboard input through the exact original, signed Alpine `fbi` APK on
x86-64, RV64 and AArch64. But `nas_vt_write` still returned `EOPNOTSUPP`:
the hardware VT could not paint a graphical text terminal. Earlier M198/M199
already proved the separate NaSeed userspace compositor with original Vim
and OpenRC service management; those are **not** a substitute for Linux fbcon.

M203's one cohesive architectural change is an independently useful physical
VT1 framebuffer console: kernel-backed 80x25 text cells, ANSI control, glyph
rendering and scroll using the existing display/fbdev class and VirtIO-GPU
commit path. The existing `/dev/tty1` line discipline and Input Core continue
to own authentic keyboard events. No duplicate GPU, keyboard, custom init,
shell, login helper or replacement browser is added.

## Real original Alpine setup

One disposable copy of M202's signed ext4 Alpine 3.23.7 installation is used.
Only `/etc/inittab` and `/etc/securetty` are adjusted with host `debugfs`:

```
tty1::respawn:/sbin/getty -L tty1 38400 linux
```

The original `ttyS0` authenticated getty remains available as a separate
physical serial observer. `/sbin/init`, `/sbin/getty`, `/bin/login`, `/bin/sh`,
`/bin/ash`, `/bin/busybox`, and original signed `/usr/bin/fbi` are SHA-256
identical to the source disk. The physical partition table is not changed,
and the new ext4 partition must pass `e2fsck` and exact config-file readback.
M201/M202's two independent PNG inputs are preserved byte-for-byte.

## Exact-head physical three-ISA acceptance

Run one native system-QEMU per architecture:

```
sh scripts/probe-m203-alpine-fbcon.sh amd64
sh scripts/probe-m203-alpine-fbcon.sh riscv64
sh scripts/probe-m203-alpine-fbcon.sh arm64
```

Each genuine original Alpine PID1/OpenRC boot must pass:

1. Original BusyBox `getty` prints an actual `naseed login:` onto physical
   800x600 QEMU VirtIO-GPU scanout. QMP `screendump` must independently
   observe real 80x25 fbcon white-on-black glyph pixels; host UART text is
   not accepted as display proof.
2. QMP VirtIO keyboard types `root`, receives the physical password prompt,
   types the real test password, then original Alpine login starts original
   ash on *graphical tty1*. Execution of original `/bin/login` and `/bin/sh`
   is independently witnessed on kernel UART.
3. That genuine graphical shell receives a second physical keyboard command
   and writes `/root/m203-typed-shell.txt` to real ext4. The separate serial
   shell reads and verifies the literal file contents.
4. The graphical ash starts the exact original signed `/usr/bin/fbi`
   **interactively**. An independent QMP screendump checks image 1 pixels,
   physical Space changes to image 2 with four different exact pixel colors,
   and physical `q` quits. `KD_GRAPHICS` must suppress fbcon painting over
   the program's framebuffer while preserving VT text state.
5. `KD_TEXT` must restore physical VT text glyph pixels. The original shell
   then receives a physical `echo $?` command, records genuine `fbi` exit 0,
   writes a second distinct ext4 file after graphics relinquish, and the
   authenticated separate serial shell validates both the original exit
   status and second durable payload.

One failure in a real pixel, keyboard input, ELF provenance, genuine Alpine
password authentication, VT ownership or ext4 readback is a failure, not an
occasion to synthesize a guest PASS marker. A host success sentinel is only
written **after** real QMP screenshots and original guest ext4 checks. The
targeted workflow saves every physical QMP frame, original UART and runner
logs, signed ELF hashes and host QMP failures for triage.

M203 deliberately does not claim multiple switchable VTs, a DRM driver,
X11, Wayland, or a full Linux kernel text font database. The milestone is
honest, observable native graphical console ownership and original Alpine
terminal interactions on the existing GPU and keyboard architecture.
