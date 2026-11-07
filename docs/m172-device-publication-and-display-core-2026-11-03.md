# M172: device publication and replaceable display backend

Date: 2026-11-03. Development line: `naseed-app-batch2`, Draft PR #148.

## Why this batch exists

NaSeed already has a common Bus/Driver registry, BlockDevice, NetDevice,
Input Core/evdev, VFS/OpenFile and the userspace desktop. However the Linux
syscall fallback still knew individual `/dev/null`, `/dev/tty`, `/dev/fb0`,
`/dev/ptmx` and PTY slave spellings. Display I/O was tied directly to one
Limine boot surface. Both made it too easy to add another device by editing
generic filesystem dispatch instead of registering it with a class.

M172 establishes *publication and ownership* rather than new device features.
It does not attempt complete Linux devtmpfs, DRM/KMS, GPU acceleration, runtime
display switching, ACPI, SMP or physical hardware hotplug.

## Device publication ownership

- `kernel/drivers/core/chrdev.[ch]` owns a single registry of immutable,
  owner-provided device node identities plus dynamic provider callbacks.
  Registration rejects duplicate paths; presence checks run at lookup and
  OpenFile pin time; unregister refuses pinned nodes with `-EBUSY`.
- Pseudo endpoints are published by `kernel/drivers/core/misc.c`; console/TTY
  and PTY names by their respective drivers; fb0 by the display class adapter.
  Dynamic evdev retains its provider interface for `/dev/input/eventN`.
- `kernel/fs/syscall.c` no longer decides which literal `/dev` path names are
  supported. After *normal VFS namei* fails, the bootstrap compatibility path
  consults the class registry once; an actual mounted inode always wins.
- Each newly opened shared `nas_open_file` takes one optional node pin. Dup,
  fork and SCM_RIGHTS share the existing File identity and do not create extra
  node pins. Final File close drops the node pin after its backend release.
  A failed open rolls back the unpublished pin.

This is a small **devfs publication boundary**, not a claim that a mounted
Linux-equivalent devtmpfs or sysfs hierarchy has already been implemented.
Keeping the VFS miss fallback is intentional while NaSeed still supports
direct-Limine ELF/rootless bootstrap profiles.

## Display ownership

- `kernel/drivers/display/display.h` defines a minimal `DisplayDevice`, mode,
  scanout byte operations, primary registration and query.
- The existing Limine-owned framebuffer is the first backend. Hardware pages
  remain firmware-owned; no user physical address or new mmap ABI is exposed.
- Linux `/dev/fb0` operations remain an adapter above the selected display,
  independent of how its backend was discovered. A second mock provider in
  the host tests proves the interface need not know Limine.
- The initial class has one stable primary device. There is no mutable
  display hot-unplug or modeset API, and no new kernel GUI syscall.

## Verification boundary

Focused host gate: `sh scripts/check-m172-device-display.sh`.
It uses the production node registry (duplicate registration, exact path,
dynamic provider, availability, pin/`-EBUSY`/release), boot and mock display
backends, Linux fbdev UAPI/layout, M167 positioned FileOps, M106 syscall/FD
ownership, lwIP socket ABI and M166 input/evdev port. Shared-source and
architecture checks are `scripts/check-three-isa-core.py` and
`scripts/check-architecture-boundaries.py`.

Native acceptance is a separate exact-HEAD three-ISA system-QEMU matrix in
`.github/workflows/m172-batch.yml`. It reuses the established M171 real guest
image/terminal/browser/LVGL + physical QMP mouse + screen pixel oracle,
precisely to detect regressions through the new device publication path.
M171 remains available by manual dispatch; M172 is the active automated gate.

## Intentional next boundaries

1. A mounted devfs provider / device class hierarchy, once normal mounted
   namespace boot is the common default, rather than duplicating a second
   pathname walker in syscall logic.
2. Device resources (MMIO/IRQ/DMA) with typed ownership above physical buses.
3. A second actual display hardware backend and a minimal general userland
   surface/session protocol, when a real device or program demands them.

The previous VFS, netdev, block and input algorithms are unchanged. Their
existing focused real-software and three-ISA tests remain acceptance gates;
this batch is an architectural connection point, not completeness parity
with Linux.
