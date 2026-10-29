# NaSeed Path-first VFS regression contract

This checkpoint belongs to the **current** NaSeed `vfs-block-lwext4-foundation`
PR family. It changes no kernel behavior by itself. The original Bash,
BusyBox, RamFS, ext4, LittleFS and three-ISA QEMU gates remain mandatory.

## Two distinct evidence layers

1. `sh scripts/check-path-first-oracle.sh`: compile and run a **real Linux
   host** C program using actual `openat`, `renameat`, `fchdir`, `unlinkat`,
   `getcwd`, `symlinkat`, and `/proc` mount crossing. Then compile the genuine
   NaSeed `kernel/fs/{vfs,lookup,vfs_providers}.c` with controlled provider
   stubs. Neither result is a NaSeed-native guest PASS.
2. Existing native QEMU acceptance: `user/ramfs-native-vfs.c` already covers
   owned directory FD after rename, separate recreated basename, open-unlink,
   inode replacement, `getcwd`, deep path and real independent upstream RamFS;
   the `scripts/probe-vfs-*` suites cover ext4/GPT/LittleFS and no-CPIO
   independent rootless boot. Preserve these actual Linux ELF tests. After the
   new Path-first syscall routing is connected, add an explicit native guest
   comparison to the Linux oracle for unlinked cwd and directory-FD lookup;
   do not call the host fixture alone a 3-ISA acceptance.
3. `sh scripts/probe-vfs-path-first-busybox.sh amd64|riscv64|arm64`
   stages **unchanged Debian BusyBox 1.37.0** plus
   `tests/vfs-path-first-busybox-workload` through the existing native
   `BUSYBOX_EXTRA_DIR` / parity harness. It checks shell-owned cwd after a
   BusyBox child renames the directory, reads through an inherited opened
   *file* FD after unlink/replacement, `..` crossing the default writable
   tmpfs mount and default **read-only** `/proc` mount (without writing to
   procfs), and symlink/trailing-slash failures. No `NAS_ENABLE_RAMFS=1`
   kernel build flag is required. The amd64 path runs the SAME
   fetched original BusyBox binary and script on Linux before QEMU. This
   is **not** a C-level `openat(dirfd)` test: shell cwd is different from
   explicitly passing a held directory FD to openat. Preserve the existing
   freestanding `user/ramfs-native-vfs.c` test for that exact behavior.
   A missing cross-toolchain or QEMU must be recorded as *not run*, never
   converted into a passing guest marker.

| Observable contract | Linux-host oracle | Existing NaSeed coverage | New native gap |
| --- | --- | --- | --- |
| Open, unlink, recreate same pathname: two different live inodes | Real `fstat`, `pread`, link count | Host VFS + real RamFS native ELF | Add exact old/new byte comparison on ext4 after unlink |
| Rename directory while old dirfd open; recreate old path | Real `openat(dirfd)` must stay on moved inode | RamFS native ELF, host owned-Path test | New path-based VFS syscall entry must use `Path`, never stringify and relookup |
| Remove opened directory; `getcwd` and create under it fail | Linux `fchdir` succeeds, then `getcwd` and `openat(O_CREAT)` give `ENOENT` | Host stale/detached Dentry tests | Add 3-ISA native unlinked-directory probe once Path-first enters syscall layer |
| `file/.`, `file/..`, symlink to non-directory with trailing `/` | `ENOTDIR` | Host namei fixture; original BusyBox path applets | Native guest assert exact errno for Path-first walker |
| Dangling symlink and `O_CREAT`; `O_EXCL` | Follow destination vs `EEXIST` | Host namei/create-target regression, native ext4 symlink test | Keep exact old/next-step results on real file backend |
| Cross a real mount and return with `..` | Linux `/proc/..` returns root inode/mount | Host VFS mount+covered Dentry, QEMU physical ext4 | Test `Path` root clamp and mount parent under native mkdir/rename |
| Valid full path longer than 255 bytes | Real Linux host path >255 succeeds | **Currently unsupported by NaSeed's 256-byte string ABI** | Do not enable NaSeed positive gate until `NAS_VFS_USER_PATH_MAX` + usercopy + VFS consumers are end-to-end ready |
| Boot without CPIO, direct-ELF executes real ext4 ELF | Not applicable (Limine-specific) | `scripts/probe-vfs-exec-no-cpio-x86.sh` | Repeat after Path-first root and mount crossing change |

The host provider regression now invokes the **actual** NaSeed functions
`nas_fs_lookup_parent_at_policy`, `nas_fs_lookup_path_at`,
`nas_vfs_open_path_at`, `nas_vfs_{unlink,rename,link}_child`,
`nas_vfs_rename_child_with_intent`, `nas_vfs_link_path_child`, and
`nas_vfs_{create,mkdir,symlink}_child`. It checks:

- `file/` and `symlink-to-dir/` cannot be unlinked/renamed; source names
  and inode identity survive. Operation-specific destination trailing slash
  errno matches a separately compiled Linux syscall oracle for mkdir,
  linkat, symlinkat and renameat.
- Same-inode hardlink rename leaves **both** names intact; rename with
  NOREPLACE fails with `EEXIST`; mounted/nested destinations fail `EBUSY`,
  cross-mount linking/rename fails `EXDEV`.
- An opened-but-now-unlinked directory remains introspectable through its
  owned Dentry, but `create_child`, `mkdir_child`, and `symlink_child` all
  fail `ENOENT` without creating a hidden entry. A rootless mount cannot
  manufacture a global parent by traversing `..`.
- Path-first open-at on a renamed dirfd uses its **original child inode**;
  O_CREAT follows a dangling relative symlink, O_EXCL preserves the link,
  O_PATH retains a symlink or directory Dentry, denied truncate leaves
  contents intact, rejected backend create leaves no new name, and File
  inode/path owners are relinquished independently.

These checks are host-linked NaSeed production code against controlled
backend mocks. They are **not** substitute evidence for the still-pending
three-ISA QEMU run of the unchanged BusyBox application workload. The
separate real Linux oracle exercises a valid path longer than 255 bytes;
NaSeed's longer-path positive gate remains deliberately disabled.

## Rollout gate and failure policy

- `nas_vfs_path_{get,put}` owns refs; path-returning APIs must have a
  release on *all* successful paths, and a failed call must not publish a
  partial owner. Keep negative, stale-mount, rename-after-open and creation
  failures in the fixture, not only success paths.
- Keep the same shared namei implementation. `kernel/fs/lookup.c` owns
  symlink/dot/`..`; filesystem providers receive `(parent inode,basename)`;
  no new per-provider, per-ISA or per-syscall path normalization.
- Boundary guards must inspect **persistent `mount_slot` fields**, not reject
  harmless locals named `parent_node` during conversion from Path to inode.
- The 255-byte limit is intentionally a **known incompatibility**, not a
  Linux-oracle failure and never a reason to rewrite user source or weaken
  the expected Linux result. Retain a native negative ENAMETOOLONG check
  until the complete migration supports longer user paths.
- CI must distinguish Linux-oracle PASS, NaSeed-host PASS, and real three-ISA
  QEMU PASS. Only the final same-commit native suite is release evidence.
