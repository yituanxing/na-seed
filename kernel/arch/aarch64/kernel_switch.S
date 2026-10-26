.text
.globl nas_arch_kernel_swap
.type nas_arch_kernel_swap,%function
nas_arch_kernel_swap:
    /* AAPCS64 integer callee-saved GPRs; kernel C has no SIMD usage. */
    sub sp,sp,#96
    stp x19,x20,[sp,#0]
    stp x21,x22,[sp,#16]
    stp x23,x24,[sp,#32]
    stp x25,x26,[sp,#48]
    stp x27,x28,[sp,#64]
    stp x29,x30,[sp,#80]
    mov x9,sp
    str x9,[x0]
    ldr x9,[x1]
    mov sp,x9
    ldp x19,x20,[sp,#0]
    ldp x21,x22,[sp,#16]
    ldp x23,x24,[sp,#32]
    ldp x25,x26,[sp,#48]
    ldp x27,x28,[sp,#64]
    ldp x29,x30,[sp,#80]
    add sp,sp,#96
    ret
.size nas_arch_kernel_swap,.-nas_arch_kernel_swap
.section .note.GNU-stack,"",%progbits
