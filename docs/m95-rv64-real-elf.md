# M9.5: actual RV64 external ELF PT_LOAD and BSS execution

Unlike the previous baked-in machine-code U-mode smoke, this milestone builds
an **independent static RISC-V ET_EXEC ELF64**, copies it onto the QEMU UEFI
ESP, and passes it via a real Limine module into the kernel.

- `user/riscv64/exit.S`: separate linker-controlled text (RX) and
  data+BSS (RW). The program reads a file-backed 42, checks the zero-filled
  BSS, then performs Linux RV64 `exit(42)`; bad BSS exits(1) and fails CI.
- `arch/riscv64/elf_image.c`: validates ELF64 class/endianness,
  `EM_RISCV=243`, fixed static ET_EXEC, bounded program-header array,
  file offsets, file/mem extents, W^X, executable entry, physical/page
  alignments and nonoverlap. It allocates real frames, maps per-segment
  4KiB RISC-V PTEs, copies file-backed bytes and leaves BSS zero.
  Allocation failures rollback page-table entries and owned data frames.
- `usermode.c` constructs a fresh SV39/SV48/SV57 root, shares only
  Limine's kernel upper-half entries, maps stack and the validated ELF,
  switches hardware SATP, executes `sret` to the ELF header's e_entry,
  and verifies a real user ECALL. **No embedded fake user instruction bytes.**
- Dedicated genuine QEMU/OpenSBI/EDK2 CI requires the fresh ELF marker as
  well as the established SATP trap and page-table selftests.
- AArch64 user PTE/boot and x86 BusyBox/Bash remain independent regressions.

**Explicit scope:** a bounded first static RV64 ET_EXEC image, 1MiB ELF
file limit, a test-only low VA region, one-shot exit, single core, no
dynamic linker or general-purpose `execve`/signal delivery, no general
syscall dispatcher, no page fault handling or task scheduling. The
cross-architecture ELF64 structural header is reused but actual machine
ABI decisions remain in `arch/riscv64/`; do not widen the x86 executable
format without a real workload/test.
