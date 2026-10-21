#include "../kernel/nr.h"
#include "../kernel/errno.h"
/* Supplementary direct Linux syscall ABI probe. Real unmodified Alpine
 * BusyBox remains the primary end-to-end shell regression. This probe
 * checks exact openat flags and errno that shell syntax cannot express.
 */
.text
.global _start
_start:
    sub $32, %rsp
    /* An EXISTING read-only file can be opened with O_RDONLY|O_CREAT. */
    mov $__NR_openat, %rax
    mov $-100, %rdi
    lea hostname(%rip), %rsi
    mov $0x40, %rdx
    mov $0644, %r10
    syscall
    cmp $3, %rax
    jl failed
    mov %rax, %r12
    mov $__NR_read, %rax
    mov %r12, %rdi
    lea 0(%rsp), %rsi
    mov $1, %rdx
    syscall
    cmp $1, %rax
    jne failed
    cmpb $'n', 0(%rsp)
    jne failed
    mov $__NR_close, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    jne failed
    /* O_CREAT|O_EXCL existing file has EEXIST, not EROFS. */
    mov $__NR_openat, %rax
    mov $-100, %rdi
    lea hostname(%rip), %rsi
    mov $0xc0, %rdx
    mov $0644, %r10
    syscall
    cmp $-EEXIST, %rax
    jne failed
    /* A true modification must still fail on immutable CPIO. */
    mov $__NR_openat, %rax
    mov $-100, %rdi
    lea hostname(%rip), %rsi
    mov $0x201, %rdx
    xor %r10, %r10
    syscall
    cmp $-EROFS, %rax
    jne failed
    /* Creation of a MISSING CPIO regular file remains forbidden. */
    mov $__NR_openat, %rax
    mov $-100, %rdi
    lea missing(%rip), %rsi
    mov $0x40, %rdx
    mov $0644, %r10
    syscall
    cmp $-EROFS, %rax
    jne failed
    /* Intermediate applet symlink resolves to a regular ELF. It cannot
     * act as a directory merely because lexical .. removes its name. */
    mov $__NR_openat, %rax
    mov $-100, %rdi
    lea badwalk(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp $-ENOTDIR, %rax
    jne failed
    mov $__NR_write, %rax
    mov $1, %rdi
    lea passed(%rip), %rsi
    mov $(passed_end-passed), %rdx
    syscall
    cmp $(passed_end-passed), %rax
    jne failed
    mov $__NR_exit_group, %rax
    xor %rdi, %rdi
    syscall
failed:
    mov $__NR_exit_group, %rax
    mov $1, %rdi
    syscall
hostname: .asciz "/etc/hostname"
missing: .asciz "/etc/missing-abi"
badwalk: .asciz "/bin/cat/../busybox"
passed: .ascii "[USER] Linux openat CPIO flags and pathwalk ABI PASS\n"
passed_end:
