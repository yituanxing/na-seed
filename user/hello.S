.text
.global _start
_start:
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
