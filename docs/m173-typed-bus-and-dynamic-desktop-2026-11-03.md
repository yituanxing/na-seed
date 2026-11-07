# M173: typed device resources and an open desktop-client boundary

The purpose of M173 is to make the next driver or user application attach to
NaSeed's **existing** mechanisms without adding a parallel kernel subsystem.
This is architecture establishment, not a claim of complete Linux driver
management or a Wayland desktop.

## Device resource ownership

- `nas_bus_device.resource` is a tagged, borrowed platform resource view.
  VirtIO-MMIO reports mapped registers and their length; PCI reports a stable
  configuration snapshot, BDF and which BARs were mapped. Bus registration
  validates the kind against the bus, basic dimensions and mapped resources.
- `nas_bus_device.virtio` is the typed, borrowed `nas_virtio_transport`
  endpoint used by the original U-Boot block/net ports and Linux VirtIO Input.
  It replaces the class drivers' casts from the old untyped `context`.
- The PCI/MMIO platform enumerators still own address mapping and the storage
  behind the resource view; the Bus Registry still owns binding and deferred
  removal. Existing VirtIO Transport, DMA Provider, BlockDevice and NetDevice
  reference/retirement mechanisms are unchanged. No independent DMA or IRQ
  framework was introduced.
- The existing `test-driver-bus` paths retain late binding, asynchronous
  removal, pending-device exclusion and mounted-block pins. New negative tests
  reject bus/resource kind mismatches, invalid PCI metadata and invalid MMIO
  spans before a driver can probe.

## Ordinary userspace desktop clients

- The compositor still owns `/dev/fb0` and evdev. The first four historical
  clients continue to bootstrap through fork/exec and private AF_UNIX
  socketpairs, retaining M167-M171 acceptance behavior.
- In M173 mode the compositor additionally listens at
  `/tmp/naseed-desktop.sock` (or `NASEED_DESKTOP_SOCKET`) through the existing
  pathname AF_UNIX implementation. A separate ELF launched later by a normal
  BusyBox shell can connect, announce a surface and transfer its actual memfd
  using `SCM_RIGHTS`; the compositor allocates a free window slot. Application
  kind and window slot are independent, so a second LVGL client is valid.
- The compositor limits outstanding clients and reads an external client's
  surface offer incrementally and nonblocking. It checks the control message,
  input capabilities, dimensions and exact regular-file backing size before
  mapping; bad or disconnected peers release their accepted FDs without
  terminating the compositor. Live clients use a separately tracked incremental
  packet reader. The compositor never calls `waitpid()` on external children
  belonging to the shell.
- Pointer/keyboard routing is based on the focused window's advertised input
  capability, not a hard-coded role index. One owner removes window mappings,
  descriptors, z-order entries and focus on close/disconnect.
- Existing clients' inherited descriptor 3 is selected explicitly by an
  environment marker; the shell clears it before running unrelated programs.

## Focused acceptance and evidence

Host command: `sh scripts/check-m173-batch.sh`.

Native target command: `sh scripts/probe-m173-batch.sh amd64`, `riscv64` or
`arm64`. The gate reuses the M171 signed Alpine overlay and pinned upstream
LVGL. QMP operates actual VirtIO input and reads the physical QEMU display:

1. Four independently executing windows are visible.
2. QMP focuses the real PTY terminal and types `gui` into BusyBox; `/usr/bin/gui`
   is a filesystem alias for the existing LVGL ELF, not a compositor spawn ID.
3. The resulting externally owned, second LVGL process contributes a fifth
   memfd-backed window through the listener. QMP verifies different physical
   screenshot pixels and live LVGL text/button pixels.
4. QMP clicks the external window's real button, verifies repaint pixels, and
   closes **only** this window. The prior four windows remain visible.
5. QMP types `echo 3` into the surviving shell; screenshot differences verify
   the PTY still works, then the desktop exits and reaps owned children.

There are five physical PPM frames per target (`four`, `five`, `clicked`,
`closed`, `shell-after`), original application execution records, exact guest
markers and archived logs. Host tests cover typed Bus resources, existing
Block/Net/Input, M172 node/display ownership, legacy GUI and malformed surface
offers. The current scratch container rejects pathname AF_UNIX socket creation
with `EPERM` and has no initialized LVGL submodule; the final host+three-ISA
verification must therefore run on the GitHub Actions runner and real QEMU.

## Deliberately out of scope

This is not a mounted devfs (M172 still uses VFS-priority bootstrap publication),
DRM/KMS, hot-pluggable PCIe enumeration, an IRQ domain, a polished GUI/window
protocol, or an Alpine package manager. Resource views are borrowed and
bounded; desktop seats/windows remain limited. Future backend and application
work must use these existing contracts rather than add path-name or role-name
special cases inside the kernel.
