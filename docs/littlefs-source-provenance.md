# Upstream littlefs as an independent filesystem control variable

Pinned **original, unmodified** littlefs v2.11.2,
commit `adad0fbbcf5382c20978d07f94f9c13be9041c1b`,
license **BSD-3-Clause**. The five upstream files in `third_party/littlefs`
are byte-identical to this immutable GitHub revision.

This is NOT a replacement for NaSeed's RAM tmpfs: littlefs is a log-based,
power-loss resilient filesystem for block-erasable flash storage, whereas
tmpfs is volatile VFS memory. We choose it over Apache NuttX's tmpfs
because of kernel API and licensing compatibility, and because it allows
testing two independently written filesystems (lwext4 vs littlefs) behind
exactly the same NaSeed BlockDevice without modifying either upstream.

`ports/littlefs/block_adapter.c` converts 512-byte sector read/write/flush
into littlefs 4096-byte erase + 256-byte read/program requests; enforces
the 1->0 flash write constraint against a borrowed RAM-disk image;
rejects read-only, unsupported sectors and drivers without flush; pins
the BlockDevice during the mounted filesystem's lifetime.

`scripts/check-littlefs-block.sh` exercises actual upstream library
format, mkdir, write, sync, rename, readdir, unmount/re-mount, read back and
unlink on the SAME kernel BlockDevice and memory backend already exercised
by lwext4. This **is an upstream filesystem -> shared BlockDevice
acceptance**, not yet a mounted NaSeed VFS provider, real guest native
syscalls, POSIX inode identity, unlink-open or production crash recovery.
Upstream littlefs is path-oriented and has no exported stable inode
number; it would be dishonest to fabricate that in the VFS. The next
VFS lifecycle batch must expose stable owned per-open objects before
wiring a writable littlefs VFS provider.
