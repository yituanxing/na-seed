# Genuine upstream littlefs mounted through NaSeed VFS (read-only)

The byte-identical BSD-3-Clause littlefs v2.11.2 source now has a second
provider under `ports/littlefs/vfs_port.[ch]`. It uses the actual
`kernel/fs/vfs.c`, namei and BlockDevice + adapter, not a mock filesystem.
Mounted read-only until open-File/orphan inode lifetime supports write,
unlink and rename correctly on all filesystems.

Littlefs does NOT expose persistent Linux inode numbers. For this
immutable mount, a bounded per-mount identity table assigns unique
collision-free monotonic VFS inode IDs, held until unmount. These are
in-memory identities only, never mistaken for littlefs on-disk inode IDs.

The original `lfs_file_opencfg` receives a local file cache and the mount
owns its read/prog/lookahead buffers. No hosted malloc in the provider;
original littlefs is untouched. The host VFS acceptance formats a real
littlefs image, writes, unmounts, mounts it through NaSeed's actual VFS,
reads files, checks inode identity shared with readdir, rejects writes,
pins an open inode to prevent unmount, unmounts, remounts and verifies bytes.

Verified coverage is **host VFS integration**, not yet a booted native
NaSeed guest Linux ELF nor production crash consistency. This new provider
does not solve general POSIX writable unlink-open, dentry caching or IRQ
storage concurrency.
