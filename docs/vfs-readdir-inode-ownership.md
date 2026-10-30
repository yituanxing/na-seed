# VFS directory iteration by inode (Linux/NaOS ownership audit)

This change removes the **global opened_path parameter** from the VFS
`read_dir` operation and all bootfs/tmpfs/procfs/lwext4/LittleFS adapters.

## What is now genuinely common

- A shared refcounted `File` owns its `(mount,inode)` and a logical
  directory cursor. `getdents64` translates the provider-independent
  `nas_vfs_dirent` records to the Linux user ABI.
- Filesystems receive the owning inode, cursor and a kernel output buffer.
  They never reopen or retarget a directory by the original pathname.
- tmpfs recognizes its borrowed immutable `/tmp` root through inode
  identity; renamed directories find their live name through provider
  dentries, not the File's stale original path.
- procfs introduces a single `nas_proc_directory_from_ino` decoder used
  both by child lookup and directory enumeration. In particular, opening
  `/proc/self` pins the original PID/TGID; inherited descriptors enumerate
  that same inode after fork rather than changing targets when a new task
  resolves the literal `/proc/self` string.
- ext4 and LittleFS already enumerate by provider inode and never needed
  an absolute VFS path in their `read_dir` callbacks. Remove the unused
  parameters instead of retaining ceremonial compatibility shims.
- Readonly bootfs enumerates its internal CPIO inode children independently
  of the global mountpoint. No vendored third-party code is modified.

## Still to migrate before a complete Linux-style VFS

`lookup_child` and `read_dir` are now inode-relative. However the current
mountpoint selector uses canonical pathname strings, VFS namei retains a
bounded canonical hint, and namespace **mutations** (`mkdir/unlink/rename`)
still send absolute paths into providers. Path-based `readlink` is also
transitional because the upstream public lwext4 API is pathname-based.
A real refcounted `(mount,dentry)` path object, provider-local parent/name
mutations, and inode eviction/dcache invalidation are still required.

This batch does NOT silently mark the inode/dentry/mount refactor complete.
The next structural change must preserve mount pin ownership across
openat/fchdir/rename and all three ISA real-QEMU regressions.
