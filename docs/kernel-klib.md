# Kernel libc subset (freestanding klib)

NaSeed has **two independent runtime domains**:

- **Kernel:** `kernel/lib/string.c` supplies ten standard C memory/string
  primitives using freestanding C. The kernel **never links musl or glibc**.
- **User space:** Linux-native glibc/musl binaries (including BusyBox) continue
  to run against the unchanged Linux syscall ABI. This is not a new userspace
  libc, and no custom `malloc`, `stdio`, or POSIX syscall wrappers were added.

The standard `memcpy/memmove/memset/memcmp` symbols are required by compiler
lowering even with `-ffreestanding -fno-builtin`. Their definitions must
not recursively lower into external libc calls. The baseline implementation
intentionally uses byte loops with volatile access for copy/fill to prevent
that recursion, favoring correctness over premature ISA-specific tuning.

The ten provided entry points are:

`memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strnlen`,
`strcmp`, `strncmp`, `strchr`, `strrchr`.

The existing Task/VM/TTY/tmpfs/exec/FS modules now consume these shared
primitives where appropriate. **Do not** replace `mm/usercopy.c`:
user-pointer reads and writes must still validate address ranges, mapping
permissions and page boundaries before copying.

The portable C implementation is local and does not import third-party code;
musl's low-level string functions were reviewed as a possible source, but
this subset is small enough to implement and exhaustively test here.
If musl code is adopted later, preserve applicable copyright/license notices.

## Tests

`sh scripts/check-klib.sh`:

1. Runs native semantic tests across 192 lengths and 16 alignments, checking
   untouched canaries, all overlap directions and unsigned-byte comparisons.
2. Cross-compiles the same kernel source for x86-64, RISC-V64, and AArch64
   with freestanding `-O2` flags.
3. Uses `nm -u` to reject **any** undefined references from the primitive
   implementation object (in particular recursive `memcpy/memset`).
4. Is also part of `scripts/preflight.sh`, ahead of full-kernel/QEMU jobs.

The x86 Makefile and the common full-kernel source manifest each include
exactly the same `kernel/lib/string.c` translation unit. Existing original
BusyBox, interactive TTY and VM regressions remain in the CI contract.
