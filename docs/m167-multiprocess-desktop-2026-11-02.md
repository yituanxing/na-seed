# M167: a userspace-owned, multi-process Linux-ABI desktop

## Architectural boundary

M167 makes the first desktop composition pass entirely in **userspace**. The
kernel continues to expose Linux-shaped, general-purpose primitives rather
than acquiring a window manager or app-specific syscalls:

1. The `naseed-desktop` ELF exclusively opens `/dev/fb0` and the three evdev
   devices (keyboard, absolute tablet, relative mouse). The existing VirtIO
   input driver remains the sole hardware owner.
2. The desktop separately `fork`/`exec`s `naseed-image` and `naseed-terminal`.
   These are **three independently linked ELF executables**, not three modes
   of one acceptance program. Clients do not inherit framebuffer/input FDs.
3. Each client creates a Linux `memfd_create` file, `ftruncate`s it, and
   renders to a shared `MAP_SHARED` pixel surface. It sends the descriptor
   to the desktop over a Unix-domain socket using `SCM_RIGHTS`. Only the
   desktop maps those surfaces read-only and composites the current stacking
   order, focus and titlebar dragging into physical pixels.
4. The image client runs the **original signed Alpine ImageMagick** program
   against an actual PPM file and decodes its RGB output into its surface.
   The terminal client opens a real PTY, launches the real BusyBox shell,
   executes the **original signed Alpine dialog** program, forwards focused
   keyboard input into the PTY, and commits its updated surface by IPC.
5. The desktop writes the composed framebuffer with positional `pwrite`,
   testing the general FileOps enhancement in real userspace. Character
   devices opt in to new `read_at`/`write_at` callbacks; nonseekable devices
   retain `ESPIPE`, ordinary VFS files retain their existing read/write-at
   path, and positional I/O never changes the `dup`/`fork`-shared file offset.

This is deliberately a **small local surface/seat protocol** with strict
roles and messages. It is neither claimed to be Wayland-compatible nor a
replacement for a full compositor protocol. The boundaries make a future
public protocol possible without changing kernel interfaces.
Surface handshakes use `poll(POLLIN)` before `recvmsg(SCM_RIGHTS)`; the
current NaSeed syscall path does not yet park an otherwise blocking
`recvmsg` on an empty Unix socket. The readiness-based design avoids
pretending that this unimplemented general case already works.

## Acceptance

- `scripts/check-m167-fb-pwrite.sh` exercises the real FileOps/firmware
  framebuffer provider, offset policy, 1 KiB chunking, usercopy behavior,
  shared-description cursor independence, and `ESPIPE` for other devices.
- `scripts/check-m167-desktop.sh` compiles three separate static native
  desktop ELFs and executes the production compositor's host self-test:
  real fork/Unix `SCM_RIGHTS`/memfd surface, overlap, focus, hit-testing,
  bounded tablet scaling and live shared-surface repaint.
- `.github/workflows/m167-batch.yml` invokes three-ISA native QEMU acceptance
  (amd64, riscv64, arm64). QEMU's hardware input sequence raises the image,
  drags it, raises the terminal and types into its PTY. The independent QMP
  oracle captures and validates **five physical framebuffer screenshots**.
  Serial markers are synchronization signals, **not** proof of visible pixels.
- Each architecture stages three additional **original signed Alpine ELFs**:
  `dialog`, `links`, `w3m`, disjoint from previous milestone manifests.
  The signed ImageMagick program reused from earlier milestones and the new
  `dialog` ELF are actually executed. The three `naseed-*` programs are
  newly compiled native applications and **not** counted as signed Alpine.

## Intentionally deferred

No hotplug machinery, cursor acceleration, event-loop threads, VSync,
full keyboard keymap, multi-client lifetime policy, fonts, GPU acceleration,
or network/window-server ABI is introduced solely to pass this milestone.
LVGL's upstream Linux framebuffer backend can use `pwrite` when mmap is
disabled, but binding an LVGL application directly to `/dev/fb0` would
violate the desktop's single-writer rule. The next integration should make
LVGL draw inside a client-owned shared surface and speak the userspace
surface protocol, while desktop remains the only framebuffer owner.

Native three-ISA QEMU/QMP evidence, not the host self-test alone, is required
before calling the milestone hardware-accepted.
