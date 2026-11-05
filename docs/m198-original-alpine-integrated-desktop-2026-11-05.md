# M198 — original Alpine PID1 with genuine hardware GUI and persistent editor

## What this integrates

M190 proved real QEMU keyboard, tablet and mouse events, VirtIO-GPU 2D,
the existing NaSeed userspace compositor, and **original Alpine Vim/Nano**
editing a physical `/async` ext4 volume. That was a custom CPIO guest with
the M174 test launcher as PID1. M197 separately proved a completely original
Alpine v3.23 `/sbin/init` as PID1 on persistent physical ext4 `/`, OpenRC,
password-authenticated login, public DNS+HTTPS APK signatures, and real
Bash/Lua/SQLite/Git persistence. Both were green on amd64, riscv64 and arm64
on exact commit `d59c276` — but they had never run in the **same boot**.

M198 combines those two established capabilities without changing either
the kernel graphics/input infrastructure or the original signed distro init.

1. `probe-m197-alpine-software-cohort.sh` remains the single owner of public
   APK, login, DHCP, TCP/TLS trace, ext4 image and two-QEMU-oracle workflow.
   Opt-in `NAS_M198_INTEGRATED_DESKTOP=1` enables this integrated profile.
2. All existing M197 real user programs execute **before** desktop launch.
   During disposable-root staging, the existing signed multi-ISA Alpine APK
   Docker installer adds `imagemagick`, `lynx`, `mc`, `dialog`, `vim`, `nano`,
   `ncurses-terminfo-base` to the
   root's original package DB. It uses Alpine's original official keyring,
   `apk --arch` and official HTTPS index, with no `--allow-untrusted` or
   target-ISA install scripts. The *guest* still installs Bash/Lua/SQLite/Git
   from the public Internet and exercises them on NaSeed's real network.
   The original guest APK then independently checks all editor package entries.
3. The disposable original Alpine root receives precisely five **NaSeed-built
   static** GUI client ELFs (image, terminal, browser, pinned LVGL, desktop)
   plus the two existing PPM/HTML inputs. The signed baseline stays separate
   and is verified after the session.
4. The pre-existing `naseed-desktop m187` starts as a normal child of the
   original ash, which remains under Alpine PID1/OpenRC/getty. The existing
   Linux evdev (`/dev/input/eventN`), real VirtIO-GPU `/dev/fb0`, PTY,
   memfd and Unix SCM_RIGHTS paths are used unchanged.
5. A separate host process types **actual QMP VirtIO keyboard** keys into
   the focused physical GUI terminal, launches untouched Alpine Vim from
   `/usr/bin/vim` on the physical ext4 root, edits `/root/m198-editor.txt`,
   checks the real process PID/exit/wait4 and actual 800x600 GPU pixels,
   then physically types Ctrl+F12. Original ash checks the saved bytes.
6. A second full system-QEMU boot uses the same raw disk and its original
   BusyBox/musl to recheck the M197 functional transaction *and* the GUI
   file. An independent HOST debugfs comparison and read-only e2fsck prove
   the exact ext4 bytes, package database and unchanged signed baseline.

The three native ISA jobs must each reach BOTH:

- `[M198 PHYSICAL QMP PASS]` — genuine input/display, original ext4 editor
  ELF, image changes, PID and shell wait4, not a simulated screenshot.
- `[M198 ALPINE GUI REBOOT PASS]` — original Alpine PID1 plus all old M197
  work, persistent original editor result across reboot and host fsck.

The original M196/M197 and M179/M180/M190 workflows remain independent gates.
M198 neither claims a full X11/Wayland/DRM desktop, accelerated rendering,
framebuffer mmap, generic secondary ext4 `mount(2)`, power-loss durability,
nor permanently running OpenRC graphical services. Those are later work.

## Network-install versus GUI timing

The first end-to-end M198 run timed out on all three ISAs while the **second**
public APK transaction was still unpacking dozens of optional GUI utilities.
The independent QMP timer had incorrectly started before the long M197/public
installation phase. It was not evidence of a GPU or input failure.

The opt-in integrated profile now pre-stages the exact original signed ELF
dependencies of all four native GUI clients plus the two editors using the
existing native Docker APK installer, **without** a second
expensive QEMU-side APK install. The genuine guest-side M197 public HTTPS
transaction is unchanged. It keeps a separate first-boot QEMU/UART budget
(`NAS_M198_FIRST_BOOT_SECONDS`, default 2400), waits for the original shell's
`M198_DESKTOP_INPUTS_RC=0` readiness witness (pre-GUI deadline default 2100),
and starts the **300-second QMP input/display deadline afterward**. A QMP
failure still interrupts the UART runner immediately; this never treats a
timeout as PASS or bypasses any official APK signatures.

## Exact scope of ownership

The only existing scripts changed are the stable M197 workflow driver (a
guarded opt-in branch) and M191's optional **separate x86 build tag**. The
default paths and original signed images are unchanged. No userspace driver,
ISA backend, core VFS or scheduler mechanism is duplicated or redesigned.

For reproducibility run the M198 workflow after checkout with submodules,
cross toolchains and native QEMU installed. A host-only smoke test is:

```sh
sh -n scripts/probe-m197-alpine-software-cohort.sh \
      scripts/stage-m198-alpine-desktop.sh
python3 -m py_compile scripts/m198-alpine-uart.py \
      scripts/m198-qmp-alpine-desktop.py scripts/m198-alpine-disk-check.py
python3 scripts/test-m198-alpine-host.py
```

The host check is NOT runtime acceptance: only all three real QEMU+raw disk
jobs on the same commit establish M198 end-to-end success.
