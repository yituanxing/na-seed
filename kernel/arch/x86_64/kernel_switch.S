.text
.global nas_arch_kernel_swap
.type nas_arch_kernel_swap,@function
nas_arch_kernel_swap:
    /* Preserve SysV nonvolatile GPRs; the CALL return PC stays on stack. */
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp,(%rdi)
    movq (%rsi),%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret
.size nas_arch_kernel_swap,.-nas_arch_kernel_swap
.section .note.GNU-stack,"",@progbits
