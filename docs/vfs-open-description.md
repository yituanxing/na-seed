# NaOS-aligned shared file-description ownership

Reviewed NaOS `vfs_file.c`, `vfs.h` and per-process fd-entry layout.
Its fd-table entry references a refcounted open file object owning
file operations, shared flags and offset, inode/path identity.

NaSeed's transitional `nas_open_file` consolidates formerly separate
global arrays for shared refs/flags/offset/ops. BootFS entry and
legacy Limine module pointers now live in this same shared description,
not separately on every fd entry of every forked process.

Descriptor-local FD_CLOEXEC is retained in each task. The existing
bounded slot allocation, file kinds and all 3-ISA syscall ABI translations
remain unchanged. Last close invokes the backend release and resets
the entire file object, preventing a reused slot from inheriting stale
backend pointers. No disk FS, generic mount framework, VFS inode/dentry,
page cache, locks, or syscall fake-success paths have been introduced.

Remaining convergence work: convert per-task descriptor triplets to
one typed fd-entry, move fstat/lseek/getdents/exec to inode and path
operations, and replace /tmp prefix dispatch with mount-aware pathname
lookup, including correct symlink and .. order.
