.text
.global gdt_reload
gdt_reload:
    lgdt (%rdi)
    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    ret

.global nas_arch_enter_user
nas_arch_enter_user:
    cli
    movw $0x1b, %ax
    movw %ax, %ds
    movw %ax, %es
    pushq $0x1b
    pushq %rsi
    pushq $0x202
    pushq $0x23
    pushq %rdi
    /* Linux ELF _start ABI: RDX is rtld_fini. A static executable has no
     * dynamic linker destructor, so the finalizer must be NULL.
     * Leaving the kernel's arbitrary RDX here makes glibc call it on exit.
     */
    xorq %rdx, %rdx
    iretq

/* All userspace returns now use IRETQ. SYSRET overwrites RCX/R11 with
 * syscall PC/RFLAGS and cannot restore an arbitrary asynchronous timer
 * interrupt context. This 160-byte IRET stack image is compatible with
 * both syscall and hardware IRQ. Single CPU, interrupts masked in C.
 */
.global syscall_entry
syscall_entry:
    cld
    movq %rsp, syscall_user_rsp(%rip)
    movq %rcx, nas_x86_syscall_return_rip(%rip)
    movq %r11, nas_x86_syscall_return_flags(%rip)
    movq nas_x86_kernel_stack_ptr(%rip), %rsp
    subq $40, %rsp         /* space for RIP/CS/FLAGS/RSP/SS */
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %rbp
    pushq %rbx
    pushq %r11
    pushq %rcx
    pushq %r9
    pushq %r8
    pushq %r10
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rax
    /* Stack is now 16-byte aligned, as required before the C call. */
    movq %rsp, %rdi
    call syscall_dispatch
    movq %rax, 0(%rsp)   /* syscall return, potentially from another Task */
    movq nas_x86_syscall_return_rip(%rip), %rax
    movq %rax, 120(%rsp)
    movq $0x23, 128(%rsp)
    movq nas_x86_syscall_return_flags(%rip), %rax
    movq %rax, 136(%rsp)
    movq syscall_user_rsp(%rip), %rax
    movq %rax, 144(%rsp)
    movq $0x1b, 152(%rsp)
    jmp nas_x86_iret_restore

/* An x86 timer IRQ from Ring3 pushes the real IRET frame via TSS.rsp0.
 * Save ALL 15 GPRs, including the true interrupted RCX/R11. */
.global nas_x86_timer_isr
nas_x86_timer_isr:
    cld
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %rbp
    pushq %rbx
    pushq %r11
    pushq %rcx
    pushq %r9
    pushq %r8
    pushq %r10
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rax
    movq %rsp, %rdi
    call nas_x86_timer_irq
    jmp nas_x86_iret_restore

nas_x86_iret_restore:
    /* Same saved-GPR layout for syscall and interrupt. The frame's
     * RIP/FLAGS/RSP is authoritative even after a scheduler task switch. */
    movq 8(%rsp), %rdi
    movq 16(%rsp), %rsi
    movq 24(%rsp), %rdx
    movq 32(%rsp), %r10
    movq 40(%rsp), %r8
    movq 48(%rsp), %r9
    movq 56(%rsp), %rcx
    movq 64(%rsp), %r11
    movq 72(%rsp), %rbx
    movq 80(%rsp), %rbp
    movq 88(%rsp), %r12
    movq 96(%rsp), %r13
    movq 104(%rsp), %r14
    movq 112(%rsp), %r15
    movq 0(%rsp), %rax
    addq $120, %rsp
    iretq

.global exception_common
exception_common:
    cld
    movq %rsp, %rdi
    andq $-16, %rsp
    call exception_report
1:
    cli
    hlt
    jmp 1b

.global isr0
isr0:
    pushq $0
    pushq $0
    jmp exception_common
.global isr1
isr1:
    pushq $0
    pushq $1
    jmp exception_common
.global isr2
isr2:
    pushq $0
    pushq $2
    jmp exception_common
.global isr3
isr3:
    pushq $0
    pushq $3
    jmp exception_common
.global isr4
isr4:
    pushq $0
    pushq $4
    jmp exception_common
.global isr5
isr5:
    pushq $0
    pushq $5
    jmp exception_common
.global isr6
isr6:
    pushq $0
    pushq $6
    jmp exception_common
.global isr7
isr7:
    pushq $0
    pushq $7
    jmp exception_common
.global isr8
isr8:
    pushq $8
    jmp exception_common
.global isr9
isr9:
    pushq $0
    pushq $9
    jmp exception_common
.global isr10
isr10:
    pushq $10
    jmp exception_common
.global isr11
isr11:
    pushq $11
    jmp exception_common
.global isr12
isr12:
    pushq $12
    jmp exception_common
.global isr13
isr13:
    pushq $13
    jmp exception_common
.global isr14
isr14:
    pushq $14
    jmp exception_common
.global isr15
isr15:
    pushq $0
    pushq $15
    jmp exception_common
.global isr16
isr16:
    pushq $0
    pushq $16
    jmp exception_common
.global isr17
isr17:
    pushq $17
    jmp exception_common
.global isr18
isr18:
    pushq $0
    pushq $18
    jmp exception_common
.global isr19
isr19:
    pushq $0
    pushq $19
    jmp exception_common
.global isr20
isr20:
    pushq $0
    pushq $20
    jmp exception_common
.global isr21
isr21:
    pushq $21
    jmp exception_common
.global isr22
isr22:
    pushq $0
    pushq $22
    jmp exception_common
.global isr23
isr23:
    pushq $0
    pushq $23
    jmp exception_common
.global isr24
isr24:
    pushq $0
    pushq $24
    jmp exception_common
.global isr25
isr25:
    pushq $0
    pushq $25
    jmp exception_common
.global isr26
isr26:
    pushq $0
    pushq $26
    jmp exception_common
.global isr27
isr27:
    pushq $0
    pushq $27
    jmp exception_common
.global isr28
isr28:
    pushq $0
    pushq $28
    jmp exception_common
.global isr29
isr29:
    pushq $29
    jmp exception_common
.global isr30
isr30:
    pushq $30
    jmp exception_common
.global isr31
isr31:
    pushq $0
    pushq $31
    jmp exception_common

.section .rodata
.global exception_vectors
exception_vectors:
    .quad isr0
    .quad isr1
    .quad isr2
    .quad isr3
    .quad isr4
    .quad isr5
    .quad isr6
    .quad isr7
    .quad isr8
    .quad isr9
    .quad isr10
    .quad isr11
    .quad isr12
    .quad isr13
    .quad isr14
    .quad isr15
    .quad isr16
    .quad isr17
    .quad isr18
    .quad isr19
    .quad isr20
    .quad isr21
    .quad isr22
    .quad isr23
    .quad isr24
    .quad isr25
    .quad isr26
    .quad isr27
    .quad isr28
    .quad isr29
    .quad isr30
    .quad isr31

.section .data
.balign 8
.global nas_x86_kernel_stack_ptr
nas_x86_kernel_stack_ptr: .quad kernel_stack_top

.section .bss
.balign 16
.global nas_x86_syscall_return_rip
nas_x86_syscall_return_rip: .quad 0
.global nas_x86_syscall_return_flags
nas_x86_syscall_return_flags: .quad 0

.balign 16
.global syscall_user_rsp
syscall_user_rsp: .quad 0

.balign 16
.global kernel_stack
kernel_stack: .skip 16384
.global kernel_stack_top
kernel_stack_top:
