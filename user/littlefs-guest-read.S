#include "../kernel/nr.h"
.text
.global _start
_start:
    /* Genuine x86-64 Linux syscalls into a DIFFERENT FS provider than ext4. */
    mov $__NR_openat,%rax
    mov $-100,%rdi
    lea filename(%rip),%rsi
    xor %rdx,%rdx
    xor %r10,%r10
    syscall
    test %rax,%rax
    js fail
    mov %rax,%r12
    sub $512,%rsp
    mov $__NR_read,%rax
    mov %r12,%rdi
    mov %rsp,%rsi
    mov $128,%rdx
    syscall
    cmp $(expected_end-expected),%rax
    jne fail
    mov $(expected_end-expected),%rcx
    lea expected(%rip),%rsi
    mov %rsp,%rdi
    repe cmpsb
    jne fail
    mov $__NR_close,%rax
    mov %r12,%rdi
    syscall
    test %rax,%rax
    jne fail
    mov $__NR_openat,%rax
    mov $-100,%rdi
    lea directory(%rip),%rsi
    mov $0x10000,%rdx /* O_DIRECTORY */
    xor %r10,%r10
    syscall
    test %rax,%rax
    js fail
    mov %rax,%r12
    mov $__NR_getdents64,%rax
    mov %r12,%rdi
    mov %rsp,%rsi
    mov $512,%rdx
    syscall
    test %rax,%rax
    jle fail
    mov %rax,%r14
    xor %rbx,%rbx
scan:
    cmp %r14,%rbx
    jae fail
    movzwl 16(%rsp,%rbx),%r13d
    cmp $24,%r13d
    jb fail
    mov %rbx,%r15
    add %r13,%r15
    cmp %r14,%r15
    ja fail
    cmpl $0x676f7270,19(%rsp,%rbx)
    jne next
    cmpw $0x6172,23(%rsp,%rbx)
    jne next
    cmpb $0x6d,25(%rsp,%rbx)
    jne next
    cmpb $0,26(%rsp,%rbx)
    jne next
    mov $__NR_close,%rax
    mov %r12,%rdi
    syscall
    test %rax,%rax
    jne fail
    mov $__NR_write,%rax
    mov $1,%rdi
    lea pass(%rip),%rsi
    mov $(pass_end-pass),%rdx
    syscall
    cmp $(pass_end-pass),%rax
    jne fail
    mov $__NR_exit,%rax
    xor %rdi,%rdi
    syscall
next:
    add %r13,%rbx
    jmp scan
fail:
    mov $__NR_exit,%rax
    mov $1,%rdi
    syscall
directory:.asciz "/flash/source"
filename:.asciz "/flash/source/program"
expected:.ascii "Second independent real filesystem through NaSeed VFS"
expected_end:
pass:.ascii "[PASS] LITTLEFS real ring3 openat/read/getdents64 via generic VFS\n"
pass_end:
