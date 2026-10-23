# M43: CLONE_FS/SIGHAND shared ownership and a second authentic pthread consumer

Prior M42 supports genuine xz CLONE_THREAD/futex on amd64, RV64, ARM64, but
the task data copy left signal dispositions, current working directory and
umask task-local. Linux CLONE_SIGHAND and CLONE_FS require these objects
to be shared across all threads in the group. Use the already-established
VM-owner for those common objects, not a new per-architecture implementation.

- Any thread rt_sigaction edits the exact leader-owned disposition array.
  Blocked signal masks remain per-thread; fork from a worker snapshots current
  group signal actions, cwd and umask before creating an independent VM child.
- All threads chdir/fchdir, relative path resolution and umask access the
  same leader-owned filesystem context. An ordinary fork retains copy semantics.
- Three ISA real QEMU guests run unmodified Alpine signed zstd ELF at -T2
  on a 12MiB multi-chunk CPIO file; validate two real compression/decompression
  cycles into writable tmpfs, exact byte identity and kernel CLONE_THREAD trace.
- Preserve all previous acceptance gates. This milestone DOES NOT claim a
  complete Linux thread-group-exit / robust-list / PI futex implementation.
