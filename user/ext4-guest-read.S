#include "../kernel/nr.h"
#include "../kernel/errno.h"
.text
.global _start
_start:
    /* A real Linux ELF tests syscall -> VFS -> lwext4 -> BlockDevice. */
    mov $__NR_openat, %rax
    mov $-100, %rdi
    lea guest_path(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    sub $256, %rsp
    mov $__NR_newfstatat, %rax
    mov $-100, %rdi
    lea guest_path(%rip), %rsi
    lea 128(%rsp), %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    jne fail
    mov $__NR_read, %rax
    mov %r12, %rdi
    mov %rsp, %rsi
    mov $96, %rdx
    syscall
    cmp $(expected_end - expected), %rax
    jne fail
    mov %rax, %r13
    lea expected(%rip), %rdi
    mov %rsp, %rsi
    mov %r13, %rcx
    repe cmpsb
    jne fail
    mov $__NR_write, %rax
    mov $1, %rdi
    mov %rsp, %rsi
    mov %r13, %rdx
    syscall
    cmp %r13, %rax
    jne fail
    mov $__NR_close, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $__NR_exit, %rax
    xor %rdi, %rdi
    syscall
fail:
    mov $__NR_exit, %rax
    mov $1, %rdi
    syscall
guest_path:
    .asciz "/mnt/sandbox/hardlink"
expected:
    .ascii "NaSeed VFS and BlockDevice real lwext4 data\n"
expected_end:
