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
