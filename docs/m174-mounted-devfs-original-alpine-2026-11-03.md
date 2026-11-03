# M174: a real VFS-mounted devfs and a larger original Alpine userland

Date: 2026-11-03. Single development line: `naseed-app-batch2`, Draft PR #148.

## Purpose: establish the connection, not complete Linux devtmpfs

M172 established a **single character-device publication registry**, but
`/dev` names were still a rootless-bootstrap exception after VFS namei.
M173 established typed PCI/MMIO Bus resources and independently launched GUI
clients. M174 uses both contracts in the regular mounted filesystem path,
without adding another device registry, path walker, process/FD abstraction,
or a second terminal protocol.

## VFS and device ownership

- `kernel/fs/devfs.[ch]` provides the normal `nas_vfs_type` callbacks:
  mounted root inode, `lookup_child`, `getattr`, `read_dir`, `statfs`, inode
  validation and first/last inode pins. Default VFS mounts `/dev`, and
  explicit `/dev/input` and `/dev/pts` directories expose live children.
- `kernel/drivers/core/chrdev.[ch]` remains the **only** registry and driver
  publication owner. The devfs mount enumerates registered objects, never
  invents class metadata or owns the driver's FileOps. Evdev events publish
  concrete registry nodes; PTY slaves follow actual live allocations.
- A named character inode carries Linux `S_IFCHR`, `st_rdev`, mount ID and an
  incarnation-specific inode number. Path `stat` and an opened file's `fstat`
  must agree. Unregistering a pinned device fails with `EBUSY`; a new
  registration cannot silently reuse the old inode identity.
- Opening `/dev/...` is VFS namei and one OpenFile transaction: pin the
  mounted inode/path, resolve `rdev` to the existing registered class backend,
  open provider state, then publish an FD. Failure rolls all owners back.
  Dup/fork/SCM_RIGHTS refer to that same File. Last close releases the
  provider and VFS/node pins. `/proc/PID/fd` uses the saved VFS path.
- `O_PATH` acquires only path/inode ownership, never opens hardware;
  `O_RDWR|O_TRUNC` is valid for a character device despite the namespace
  being read-only. `O_EXCL`, directory `getdents`, `openat`, device ioctls,
  `/dev/pts/N` and the Linux device major/minor report use the ordinary ABI.
- A mounted character File still delegates positioned `pread/pwrite` to its
  device FileOps, even though its VFS inode/path are pinned. The fbdev
  scanout retains its seekable character-device length and write permission;
  neither operation mutates devfs directory bytes or its inode size. This
  distinction was verified against the real M173 framebuffer workload.
- Direct Limine/rootless bootstrap remains the narrow existing compatibility
  path, not a second resolver for mounted `/dev`.

This is a **mounted device filesystem**, not Linux devtmpfs completeness.
NaSeed's current mount registry does not support stacking/replacing an
existing mount at an identical mountpoint: mounting a second filesystem at
`/dev` returns `EEXIST`. Mount stacking is independent future work.

## Original Alpine 3.23 software pool

The existing external APK overlay is the sole host installer. M174 adds four
upstream signed packages (`dbus`, `inotify-tools`, `sysstat`, `tig`) to the
previous nine GUI-supporting packages and independently identifies **20**
new ELF program candidates across the four packages. No Alpine executable
is rebuilt, patched or substituted by a NaSeed implementation.

The guest performs deliberately separate checks:

1. **Native ELF entry only**: individually execute the 20 original commands
   with `--help`, reject timeouts, exec errors, signal exits and empty output.
   This does not establish 20 functioning Linux subsystems.
2. **Representative real behavior**: verify a D-Bus UUID's 128-bit hexadecimal
   value, and require `mpstat` to read real `/proc/stat` and display CPU `all`.
3. **Still unsupported**: inotify watch/event delivery and a full D-Bus
   session service are not claimed to work. The initial inotify and tig
   checks prove only an entry/version path.

The signed APK installation and per-ISA machine check, sha256 uniqueness,
guest manifest and exact rootfs fixture preservation fail closed at staging.

## Exact-head acceptance

- Host: `sh scripts/check-m174-devfs.sh` proves production devfs/VFS and
  mounted character OpenFile integration, plus historical core/driver owners.
- Software catalog: `sh scripts/m174-software-check-manifest.sh` verifies
  historical path de-duplication and 13 APK candidates.
- Native: `sh scripts/probe-m174-batch.sh amd64`, `riscv64`, `arm64` builds
  the real kernel + 20 original ELF overlay and starts an independent static
  `naseed-m174-devfs-launch` as PID 1 in system QEMU. That launcher tests
  real `/dev/null`, zero, framebuffer/evdev ioctls, live PTY `getdents`,
  pathname/FD identity and software child exit before replacing itself with
  M173's original desktop.
- Real QMP then drives mouse, keyboard and BusyBox PTY to launch a fifth
  upstream LVGL client. Five physically captured 800x600 frames are checked
  for visible windows, button pixels and terminal survival. Serial markers
  synchronize the observer; they are not a screenshot substitute.
- The final `.github/workflows/m174-batch.yml` is the **one** automated
  three-ISA run for this batch. M173 becomes manually dispatchable to avoid
  repeating the historical CI fleet.

Native results must be reported from the exact pushed GitHub run and its
actual logs. Host-only PASS does **not** imply 20 original ELF paths or
three-ISA real-QEMU PASS.

## Next actual architecture bottleneck

The current x86, RV64 and AArch64 kernels expose timer interrupts, but lack
a complete device IRQ path from hardware routing through controller EOI and
VirtIO transport ISR acknowledgement to a driver consumer. Input/net still
poll their completion queues. Rather than add an untested generic IRQ API,
the next resource stage should implement one end-to-end real interrupt
consumer (including safe unbind), then extract the shared typed contract.
