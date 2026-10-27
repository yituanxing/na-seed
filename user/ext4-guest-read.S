#include "../kernel/nr.h"
#include "../kernel/errno.h"
.text
.global _start
_start:
    /* A real Linux ELF tests syscall -> VFS -> lwext4 -> BlockDevice. */
    mov $__NR_openat, %rax
    mov $-100, %rdi
    lea guest_path(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    sub $256, %rsp
    mov $__NR_newfstatat, %rax
    mov $-100, %rdi
    lea guest_path(%rip), %rsi
    lea 128(%rsp), %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    jne fail
    mov $__NR_read, %rax
    mov %r12, %rdi
    mov %rsp, %rsi
    mov $96, %rdx
    syscall
    cmp $(expected_end - expected), %rax
    jne fail
    mov %rax, %r13
    lea expected(%rip), %rdi
    mov %rsp, %rsi
    mov %r13, %rcx
    repe cmpsb
    jne fail
    mov $__NR_write, %rax
    mov $1, %rdi
    mov %rsp, %rsi
    mov %r13, %rdx
    syscall
    cmp %r13, %rax
    jne fail
    mov $__NR_close, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    jne fail
    /* Enumerate the *actual* ext4 directory through Linux getdents64,
     * check for hardlink without a private test syscall or direct ext4 API. */
    mov $__NR_openat, %rax
    mov $-100, %rdi
    lea guest_directory(%rip), %rsi
    mov $0x10000, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r12
    /* dirfd-relative openat and fchdir use a generic mounted File, not
     * a CPIO directory. Both were previously unavailable on ext4. */
    mov $__NR_openat, %rax
    mov %r12, %rdi
    lea guest_relative(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r13
    mov $__NR_close, %rax
    mov %r13, %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $__NR_fchdir, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $__NR_openat, %rax
    mov $-100, %rdi
    lea guest_relative(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %rax, %r13
    mov $__NR_close, %rax
    mov %r13, %rdi
    syscall
    test %rax, %rax
    jne fail
    sub $512, %rsp
dir_next:
    mov $__NR_getdents64, %rax
    mov %r12, %rdi
    mov %rsp, %rsi
    mov $512, %rdx
    syscall
    test %rax, %rax
    jle fail
    mov %rax, %r14
    xor %rbx, %rbx
dir_scan:
    cmp %r14, %rbx
    jae dir_next
    movzwl 16(%rsp,%rbx), %r13d
    cmp $24, %r13d
    jb fail
    mov %rbx, %r15
    add %r13, %r15
    cmp %r14, %r15
    ja fail
    cmp $32, %r13d
    jb dir_skip
    cmpl $0x64726168, 19(%rsp,%rbx)
    jne dir_skip
    cmpl $0x6b6e696c, 23(%rsp,%rbx)
    jne dir_skip
    cmpb $0, 27(%rsp,%rbx)
    jne dir_skip
    mov $__NR_write, %rax
    mov $1, %rdi
    lea dir_ok(%rip), %rsi
    mov $(dir_ok_end-dir_ok), %rdx
    syscall
    cmp $(dir_ok_end-dir_ok), %rax
    jne fail
    mov $__NR_close, %rax
    mov %r12, %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $__NR_exit, %rax
    xor %rdi, %rdi
    syscall
dir_skip:
    add %r13, %rbx
    jmp dir_scan
fail:
    mov $__NR_exit, %rax
    mov $1, %rdi
    syscall
guest_directory:
    .asciz "/mnt/sandbox"
guest_relative:
    .asciz "hardlink"
dir_ok:
    .ascii "[PASS] real ext4 getdents64 via VFS\n"
dir_ok_end:
guest_path:
    .asciz "/mnt/sandbox/hardlink"
expected:
    .ascii "NaSeed VFS and BlockDevice real lwext4 data\n"
expected_end:
