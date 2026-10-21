#include "../kernel/nr.h"
#include "../kernel/errno.h"
/* This direct ring3 syscall probe has no shell fallback, so success can
 * only occur when the kernel really loads the CPIO shebang interpreter.
 * The final process is unmodified Alpine dynamic BusyBox /bin/busybox.
 */
.text
.global _start
_start:
    mov $__NR_execve, %rax
    lea missing_interpreter(%rip), %rdi
    lea test_argv(%rip), %rsi
    xor %rdx, %rdx
    syscall
    cmp $-ENOENT, %rax
    jne failed

    mov $__NR_execve, %rax
    lea plain_script(%rip), %rdi
    lea test_argv(%rip), %rsi
    xor %rdx, %rdx
    syscall
    cmp $-ENOEXEC, %rax
    jne failed

    mov $__NR_execve, %rax
    lea noexec_script(%rip), %rdi
    lea test_argv(%rip), %rsi
    xor %rdx, %rdx
    syscall
    cmp $-EACCES, %rax
    jne failed

    mov $__NR_write, %rax
    mov $1, %rdi
    lea marker(%rip), %rsi
    mov $(marker_end-marker), %rdx
    syscall
    cmp $(marker_end-marker), %rax
    jne failed

    /* On success this syscall changes CR3, loads musl+BusyBox, constructs
     * argv={"/bin/busybox","sh","/etc/naseed-stage.sh","token"}
     * and never returns to this instruction pointer. */
    mov $__NR_execve, %rax
    lea stage_script(%rip), %rdi
    lea stage_argv(%rip), %rsi
    xor %rdx, %rdx
    syscall
failed:
    mov $__NR_exit_group, %rax
    mov $64, %rdi
    syscall

.section .rodata
missing_interpreter: .asciz "/etc/naseed-missing.sh"
plain_script: .asciz "/etc/naseed-plain.sh"
noexec_script: .asciz "/etc/naseed-noexec.sh"
stage_script: .asciz "/etc/naseed-stage.sh"
token_arg: .asciz "token"
marker: .ascii "[USER] checked execve negative shebang errno\n"
marker_end:
.section .data
.balign 8
test_argv:
    .quad plain_script
    .quad 0
stage_argv:
    .quad stage_script
    .quad token_arg
    .quad 0
