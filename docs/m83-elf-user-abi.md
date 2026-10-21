# M8.3 — User program ABI and ELF groundwork

Based on NaOS's early split, establish owners for code *already present* before introducing generic loader registries.

- `task/elf.c`: original strict ELF64 loader, PT_INTERP checks, PT_LOAD mapping, interpreter bias, and task auxiliary-vector metadata. Current format checking is explicitly Linux x86-64 (machine=62, musl x86 interpreter); other ports must supply ABI checks, not silently accept all ELF.
- `task/user_stack.c`: original x86-64 Linux initial user stack, argv/envp/auxv and bootloader cmdline parser. Exposed via `task/user_stack.h` with its existing bounded args/env contract.
- `mm/usercopy.c`: checked user reads/writes/u64 and image/stack initialization writes previously in m1.c. Existing HHDM and PTE permission constraints are unchanged. The destination write still checks every byte for valid writable mappings before copy.
- `arch/x86_64/syscall.c`: real arch_prctl FS-base syscall ABI; no syscall-body duplication.
- `task/signal.c`: rt_sigaction/rt_sigprocmask storage semantics (no signal-frame delivery claim).
- m1.c still owns the FD syscall adapter, execve rollback/commit and the central syscall dispatcher. Those need separate ownership reviews before a second architecture.

Do **not** add multi-architecture stubs, pluggable schedulers, fake VFS behavior or new Linux syscall functionality. This patch targets existing code relocation, preserving real BusyBox/Bash/ash/execve/shebang regressions.

Acceptance: original full preflight and QEMU regression suite before merging.
