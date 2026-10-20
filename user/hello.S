.text
.global _start
_start:
    /* Kernel must not supply a garbage dynamic-loader finalizer. */
    test %rdx, %rdx
    jne fail
    /* Verify CPL3 so syscall acceptance cannot pass from ring zero. */
    mov %cs, %ax
    and $3, %ax
    cmp $3, %ax
    jne fail
    /* ARCH_GET_FS must reject the program's read-only .text page. */
    mov $158, %rax
    mov $0x1003, %rdi
    lea message(%rip), %rsi
    syscall
    cmp $-14, %rax
    jne fail
    /* The same syscall must work with a writable user stack address. */
    mov $158, %rax
    mov $0x1003, %rdi
    lea -8(%rsp), %rsi
    syscall
    test %rax, %rax
    jne fail
    /* write must not be able to read a kernel higher-half virtual address. */
    mov $1, %rax
    mov $1, %rdi
    movabs $0xffffffff80000000, %rsi
    mov $1, %rdx
    syscall
    cmp $-14, %rax
    jne fail
    mov $1, %rax
    mov $1, %rdi
    lea message(%rip), %rsi
    mov $(message_end - message), %rdx
    syscall
    cmp $(message_end - message), %rax
    jne fail
    mov $999, %rax
    syscall
    cmp $-38, %rax
    jne fail
    mov $60, %rax
    xor %rdi, %rdi
    syscall
fail:
    mov $60, %rax
    mov $1, %rdi
    syscall
message:
    .ascii "[USER] hello from standalone ELF via syscall\n"
message_end:
