/* Genuine x86-64 ring3 Linux syscalls against the NaSeed VFS path.
 * No internal kernel entry, host fake or direct lwext4 API used. */
#include "../kernel/nr.h"
.text
.global _start
_start:
    /* user/linker.ld deliberately maps only RX text; all syscall
     * destinations MUST use the genuine writable Linux user stack. */
    sub $512, %rsp
    mov $1, %r15d
    mov $__NR_openat, %rax
    mov $-100, %rdi
    lea guest_path(%rip), %rsi
    mov $0x242, %rdx             /* O_RDWR|O_CREAT|O_TRUNC */
    mov $0644, %r10
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $2, %r15d
    mov $__NR_write, %rax
    mov %r12, %rdi
    lea payload(%rip), %rsi
    mov $(payload_end-payload), %rdx
    syscall
    cmp $(payload_end-payload), %rax
    jne fail
    mov $3, %r15d
    mov $__NR_fsync, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $4, %r15d
    mov $__NR_lseek, %rax
    mov %r12, %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $5, %r15d
    mov $__NR_read, %rax
    mov %r12, %rdi
    mov %rsp, %rsi
    mov $(payload_end-payload), %rdx
    syscall
    cmp $(payload_end-payload), %rax
    jne fail
    mov $6, %r15d
    mov %rsp, %rsi
    lea payload(%rip), %rdi
    mov $(payload_end-payload), %rcx
    repe cmpsb
    jne fail
    mov $7, %r15d
    mov $__NR_close, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    jne fail
    /* Independent reopen proves namespace & inode survived FD close. */
    mov $8, %r15d
    mov $__NR_openat, %rax
    mov $-100, %rdi
    lea guest_path(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    mov $9, %r15d
    mov $__NR_read, %rax
    mov %r12, %rdi
    mov %rsp, %rsi
    mov $(payload_end-payload), %rdx
    syscall
    cmp $(payload_end-payload), %rax
    jne fail
    mov $10, %r15d
    mov %rsp, %rsi
    lea payload(%rip), %rdi
    mov $(payload_end-payload), %rcx
    repe cmpsb
    jne fail
    mov $11, %r15d
    mov $__NR_close, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $12, %r15d
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
    /* On failure use actual writable user stack, not RX ELF .data. */
    sub $16, %rsp
    mov %r15d, %eax
    xor %edx, %edx
    mov $10, %esi
    div %esi
    add $48, %al
    mov %al, 0(%rsp)
    add $48, %dl
    mov %dl, 1(%rsp)
    movb $10, 2(%rsp)
    mov $__NR_write, %rax
    mov $1, %rdi
    lea diagnostic(%rip), %rsi
    mov $(diagnostic_end-diagnostic), %rdx
    syscall
    mov $__NR_write, %rax
    mov $1, %rdi
    mov %rsp, %rsi
    mov $3, %rdx
    syscall
    mov $__NR_exit, %rax
    mov $1, %rdi
    syscall
.section .rodata
diagnostic:
    .ascii "[LWEXT4 GUEST RW DIAG] stage="
diagnostic_end:
guest_path:
    .asciz "/mnt/sandbox/guest-created"
payload:
    .ascii "NaSeed genuine guest RW ext4 via common File and BlockDevice\n"
payload_end:
message:
    .ascii "[PASS] guest ext4 RAM RW openat/write/fsync/lseek/read/close/reopen\n"
message_end:
