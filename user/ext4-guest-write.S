/* Genuine x86-64 ring3 Linux syscalls against the NaSeed VFS path.
 * No internal kernel entry, host fake or direct lwext4 API used. */
#include "../kernel/nr.h"
.text
.global _start
_start:
    mov $__NR_openat, %rax
    mov $-100, %rdi
    lea guest_path(%rip), %rsi
    mov $0x242, %rdx             /* O_RDWR|O_CREAT|O_TRUNC */
    mov $0644, %r10
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $__NR_write, %rax
    mov %r12, %rdi
    lea payload(%rip), %rsi
    mov $(payload_end-payload), %rdx
    syscall
    cmp $(payload_end-payload), %rax
    jne fail
    mov $__NR_fsync, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $__NR_lseek, %rax
    mov %r12, %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $__NR_read, %rax
    mov %r12, %rdi
    lea result_buffer(%rip), %rsi
    mov $(payload_end-payload), %rdx
    syscall
    cmp $(payload_end-payload), %rax
    jne fail
    lea result_buffer(%rip), %rsi
    lea payload(%rip), %rdi
    mov $(payload_end-payload), %rcx
    repe cmpsb
    jne fail
    mov $__NR_close, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    jne fail
    /* Independent reopen proves namespace & inode survived FD close. */
    mov $__NR_openat, %rax
    mov $-100, %rdi
    lea guest_path(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $__NR_read, %rax
    mov %r12, %rdi
    lea result_buffer(%rip), %rsi
    mov $(payload_end-payload), %rdx
    syscall
    cmp $(payload_end-payload), %rax
    jne fail
    lea result_buffer(%rip), %rsi
    lea payload(%rip), %rdi
    mov $(payload_end-payload), %rcx
    repe cmpsb
    jne fail
    mov $__NR_close, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $__NR_write, %rax
    mov $1, %rdi
    lea message(%rip), %rsi
    mov $(message_end-message), %rdx
    syscall
    cmp $(message_end-message), %rax
    jne fail
    mov $__NR_exit, %rax
    xor %rdi, %rdi
    syscall
fail:
    mov $__NR_exit, %rax
    mov $1, %rdi
    syscall
.section .rodata
guest_path:
    .asciz "/mnt/sandbox/guest-created"
payload:
    .ascii "NaSeed genuine guest RW ext4 via common File and BlockDevice\n"
payload_end:
message:
    .ascii "[PASS] guest ext4 RAM RW openat/write/fsync/lseek/read/close/reopen\n"
message_end:
.bss
.align 16
result_buffer:
    .skip 256
