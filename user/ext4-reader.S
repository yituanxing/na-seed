#include "../kernel/nr.h"
#include "../kernel/errno.h"
/* A real ring-3 Linux ABI consumer. No special test syscall or fake VFS.
 * The exact file was created by the upstream lwext4 host image fixture. */
.text
.global _start
_start:
    /* Verify ring 3 before any ext4 file operation. */
    mov %cs, %ax
    and $3, %ax
    cmp $3, %ax
    jne failed

    mov $__NR_openat, %rax
    mov $-100, %rdi
    lea filename(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    js failed
    mov %rax, %r12

    sub $512, %rsp
    /* fstat must use the opened VFS inode, not a fabricated CPIO entry. */
    mov $__NR_fstat, %rax
    mov %r12, %rdi
    mov %rsp, %rsi
    syscall
    test %rax, %rax
    jne failed
    mov 48(%rsp), %rax
    cmp $(expected_end-expected), %rax
    jne failed

    mov $__NR_read, %rax
    mov %r12, %rdi
    lea 256(%rsp), %rsi
    mov $(expected_end-expected), %rdx
    syscall
    cmp $(expected_end-expected), %rax
    jne failed
    lea 256(%rsp), %rdi
    lea expected(%rip), %rsi
    mov $(expected_end-expected), %rcx
    repe cmpsb
    jne failed

    /* lseek + positional state verify one shared opened VFS File. */
    mov $__NR_lseek, %rax
    mov %r12, %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne failed

    mov $__NR_read, %rax
    mov %r12, %rdi
    lea 256(%rsp), %rsi
    mov $(expected_end-expected), %rdx
    syscall
    cmp $(expected_end-expected), %rax
    jne failed

    mov $__NR_write, %rax
    mov $1, %rdi
    lea 256(%rsp), %rsi
    mov $(expected_end-expected), %rdx
    syscall
    cmp $(expected_end-expected), %rax
    jne failed

    mov $__NR_close, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    jne failed
    mov $__NR_exit, %rax
    xor %rdi, %rdi
    syscall
failed:
    mov $__NR_exit, %rax
    mov $57, %rdi
    syscall

filename:
    .asciz "/mnt/sandbox/hardlink"
expected:
    .ascii "NaSeed VFS and BlockDevice real lwext4 data\n"
expected_end:
