.text
.global _start
_start:
    call guest_main
1:  jmp 1b
.global guest_syscall
guest_syscall:
    lea guest_sys_numbers(%rip), %r9
    mov (%r9,%rdi,8), %rax
    mov %rsi,%rdi
    mov %rdx,%rsi
    mov %rcx,%rdx
    mov %r8,%r10
    syscall
    ret
.section .rodata
.p2align 3
guest_sys_numbers: .quad 257,0,1,74,8,3,60,217,258,264,81,79,266,263,262
