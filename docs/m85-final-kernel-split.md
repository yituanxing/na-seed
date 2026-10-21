# M8.5 — finish kernel subsystem split

NaOS-inspired code organization without inventing dynamic replacement or new services.

- init/main.c owns post-Limine CPU/MMU bring-up, real boot ELF selection, first user stack and stdio.
- task/exec.c preserves transactional execve image replacement, shebang rewriting, argv/envp snapshot, CLOEXEC and rollback.
- syscall/dispatch.c preserves syscall demultiplexing, blocked pipe replay, process exit and Linux -ENOSYS.
- kernel/panic.h centralizes the existing fatal message and halt; kernel/m1.c is removed.

Current limits: Linux syscall numbering, uname machine identity, stat encoding, ELF interpreter and stack layout remain x86-64 specific. No independent per-task kernel stacks, SMP, preemption or VFS redesign are claimed.

Full CI Bash, Debian/Alpine BusyBox, fork/exec/wait, pipes, SIGPIPE, FD/tmpfs, CPIO/shebang and smoke QEMU tests are required before merge.
