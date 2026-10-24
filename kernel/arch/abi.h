#ifndef NASEED_ARCH_ABI_H
#define NASEED_ARCH_ABI_H
/* Linux ELF64 target policy, not hardware-MMU PTE flags.
 * Syscall IDs and binary struct packing remain separate ABI adapters.
 */
#if defined(__x86_64__)
# define NAS_ELF_MACHINE 62U
# define NAS_ELF_INTERP "/lib/ld-musl-x86_64.so.1"
# define NAS_ELF_LIBC_ALIAS "/lib/libc.musl-x86_64.so.1"
# define NAS_UTS_MACHINE "x86_64"
#elif defined(__riscv) && __riscv_xlen == 64
# define NAS_ELF_MACHINE 243U
# define NAS_ELF_INTERP "/lib/ld-musl-riscv64.so.1"
# define NAS_ELF_LIBC_ALIAS "/lib/libc.musl-riscv64.so.1"
# define NAS_UTS_MACHINE "riscv64"
#elif defined(__aarch64__)
# define NAS_ELF_MACHINE 183U
# define NAS_ELF_INTERP "/lib/ld-musl-aarch64.so.1"
# define NAS_ELF_LIBC_ALIAS "/lib/libc.musl-aarch64.so.1"
# define NAS_UTS_MACHINE "aarch64"
#else
# error "NaSeed ELF64 ABI undefined for this architecture"
#endif
/* Temporary common userspace layout compatible with current x86 and Sv39.
 * Replace with MMU-mode capabilities before adding general VM layouts. */
#define NAS_USER_STACK_TOP 0x0000000070000000ULL
/* Native RV64/AArch64 signal restorer must stay clear of the entire main
 * stack mapping; both ISA providers use this shared virtual-address contract.
 * Fixed RX page with an unmapped gap below the bounded 1-MiB user stack. */
#define NAS_USER_SIGNAL_TRAMPOLINE 0x000000006fe00000ULL
#define NAS_ELF_DYN_APP_BASE 0x10000000ULL
#define NAS_ELF_DYN_INTERP_BASE 0x30000000ULL
#define NAS_TASK_MMAP_CURSOR 0x60000000ULL
#endif
