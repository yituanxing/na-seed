.section .text,"ax",%progbits
.global _start
.balign 4
_start:
    bl guest_main
1:  b 1b
.global guest_syscall
guest_syscall:
    adrp x9,guest_sys_numbers
    add x9,x9,:lo12:guest_sys_numbers
    ldr x8,[x9,x0,lsl #3]
    mov x0,x1
    mov x1,x2
    mov x2,x3
    mov x3,x4
    svc #0
    ret
.section .rodata,"a",%progbits
.balign 8
guest_sys_numbers: .xword 56,63,64,82,62,57,93,61,34,38,50,17,36,35
