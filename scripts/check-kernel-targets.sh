#!/bin/sh
# Real host regression + target object compilation; never native QEMU evidence.
set -eu
[ "$#" -eq 1 ] || exit 2
profile=$1

# Keep the host fixture and target source sets independent and explicit.
host_cc() {
    cc -std=c11 -O2 -Wall -Wextra -Werror "$@"
}
compile_target() {
    clang $flags -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
        -fno-builtin -fno-stack-protector -fno-pic -fno-pie "$@"
}

case "$profile" in
    m91)
        mkdir -p build/m91-kcontext
        host_cc -fno-omit-frame-pointer -fno-builtin \
                -I kernel scripts/test-m91-kcontext.c kernel/task/kernel_context.c \
            kernel/task/wait_queue.c kernel/arch/x86_64/kernel_switch.S -o build/m91-kcontext/host
        build/m91-kcontext/host ;;
    m92)
        mkdir -p build/m92-wait-queue
        host_cc -fno-builtin -I kernel \
            scripts/test-m92-wait-queue.c kernel/task/wait_queue.c \
                -o build/m92-wait-queue/host
        build/m92-wait-queue/host ;;
    m93)
        mkdir -p build/m93-futex
        host_cc -fno-builtin -I kernel \
            scripts/test-m93-futex-queues.c kernel/task/futex_wait.c kernel/task/wait_queue.c kernel/task/run_state.c \
                -o build/m93-futex/host
        build/m93-futex/host ;;
    m94)
        mkdir -p build/m94-runstate
        host_cc -I kernel \
            scripts/test-m94-run-state.c kernel/task/run_state.c \
                -o build/m94-runstate/host
        build/m94-runstate/host ;;
    m95)
        mkdir -p build/m95-kthread
        host_cc -fno-builtin -I kernel \
            scripts/test-m95-kthread.c kernel/task/kthread.c kernel/task/kernel_context.c \
            kernel/task/wait_queue.c kernel/arch/x86_64/kernel_switch.S \
                -o build/m95-kthread/host
        build/m95-kthread/host ;;
    m96)
        mkdir -p build/m96-k-sync
        host_cc -fno-builtin -I kernel \
            scripts/test-m96-k-sync.c kernel/task/k_sync.c kernel/task/kthread.c \
            kernel/task/wait_queue.c kernel/task/kernel_context.c \
            kernel/arch/x86_64/kernel_switch.S -o build/m96-k-sync/host
        build/m96-k-sync/host ;;
    m97)
        mkdir -p build/m97-k-timeouts
        host_cc -fno-builtin -I kernel \
            scripts/test-m97-k-timeouts.c kernel/task/k_sync.c kernel/task/kthread.c \
            kernel/task/wait_queue.c kernel/task/kernel_context.c \
            kernel/arch/x86_64/kernel_switch.S -o build/m97-k-timeouts/host
        build/m97-k-timeouts/host
        host_cc -fno-builtin -I kernel \
            scripts/test-sched-worker-burst.c kernel/task/scheduler_service.c \
            kernel/task/sched_core.c -o build/m97-k-timeouts/sched-worker-burst
        build/m97-k-timeouts/sched-worker-burst ;;
    m98)
        mkdir -p build/m98-sched-core
        host_cc -fno-builtin -I kernel \
            scripts/test-m98-sched-core.c kernel/task/sched_core.c \
                -o build/m98-sched-core/host
        build/m98-sched-core/host ;;
    m100)
        mkdir -p build/m100-signal
        host_cc -fno-builtin \
           -ffunction-sections -fdata-sections -Wl,--gc-sections -I kernel \
           scripts/test-m100-signal-failure.c kernel/task/signal.c \
           -o build/m100-signal/host
        build/m100-signal/host ;;
    m101)
        mkdir -p build/m101-cow
        host_cc -fno-builtin \
            -ffunction-sections -fdata-sections -Wl,--gc-sections -I kernel \
            scripts/test-m101-cow.c kernel/mm/mm.c kernel/mm/fault.c \
            kernel/mm/usercopy.c kernel/mm/remote_copy.c kernel/mm/anon_regions.c kernel/mm/heap.c \
                -o build/m101-cow/host
        build/m101-cow/host ;;
    m102)
        mkdir -p build/m102-tty-cow
        host_cc -fno-builtin \
            -ffunction-sections -fdata-sections -Wl,--gc-sections -I scripts/m89-test-include -I kernel \
            scripts/test-m102-tty-cow.c kernel/fs/file_io.c kernel/drivers/tty.c \
            kernel/drivers/display/fbdev.c \
            kernel/drivers/tty_ldisc.c -o build/m102-tty-cow/host
        build/m102-tty-cow/host ;;
    m103)
        mkdir -p build/m103-tid
        host_cc -ffunction-sections -fdata-sections \
            -fno-builtin -I kernel scripts/test-m103-thread-tid-lifecycle.c \
            kernel/task/process.c -Wl,--gc-sections -o build/m103-tid/host
        build/m103-tid/host
        host_cc -fno-builtin \
            scripts/test-m103-linux-clone-oracle.c -o build/m103-tid/linux-oracle
        build/m103-tid/linux-oracle
        host_cc -pthread \
            scripts/test-m103-guest-tid-lifecycle.c -o build/m103-tid/guest-host
        build/m103-tid/guest-host ;;
    *) exit 2 ;;
esac

archs='riscv64 aarch64'
if [ "$profile" = m103 ]; then
    # CI may not opt out. Explicit host-only diagnostics never emit target PASS.
    if [ "${NAS_M103_HOST_ONLY:-0}" = 1 ]; then
        echo '[M103 HOST-ONLY] three target TUs not compiled (explicit local override)'
        exit 0
    fi
    command -v clang >/dev/null 2>&1 || {
        echo '[M103 TARGET FAIL] clang unavailable (set NAS_M103_HOST_ONLY=1 for local host-only diagnostics)' >&2
        exit 1
    }
    archs='x86_64 riscv64 aarch64'
fi
for arch in $archs; do
    case "$arch" in
        x86_64) flags='--target=x86_64-none-elf -mno-red-zone' ;;
        riscv64) flags='--target=riscv64-unknown-elf -march=rv64gc -mabi=lp64 -mno-relax -mcmodel=medany' ;;
        aarch64) flags='--target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only' ;;
    esac
    case "$profile" in
        m91)
            compile_target \
                -c kernel/task/kernel_context.c -o "build/m91-kcontext/$arch-context.o"
            clang $flags -c "kernel/arch/$arch/kernel_switch.S" \
                -o "build/m91-kcontext/$arch-swap.o"
            nm "build/m91-kcontext/$arch-swap.o" | grep -q 'nas_arch_kernel_swap'
            echo "[M91 TARGET PASS] $arch native resumable kernel assembly" ;;
        m92)
            compile_target \
                -c kernel/task/wait_queue.c -o "build/m92-wait-queue/$arch.o"
            echo "[M92 TARGET PASS] $arch generic wait queue" ;;
        m93)
            compile_target \
                -c kernel/task/futex_wait.c -o "build/m93-futex/$arch.o"
            echo "[M93 TARGET PASS] $arch" ;;
        m94)
            compile_target \
                -c kernel/task/run_state.c -o "build/m94-runstate/$arch.o"
            echo "[M94 TARGET PASS] $arch generic wake state" ;;
        m95)
            for src in kernel/task/kthread.c kernel/task/kthread_boot.c; do
                name=${src##*/}
                compile_target \
                    -I kernel -c "$src" -o "build/m95-kthread/$arch-$name.o"
            done
            echo "[M95 TARGET PASS] $arch kernel worker and native boot scenario" ;;
        m96)
            compile_target -I kernel \
                -c kernel/task/k_sync.c -o "build/m96-k-sync/$arch.o"
            echo "[M96 TARGET PASS] $arch kernel semaphore/mailbox" ;;
        m97)
            for src in kernel/task/kthread.c kernel/task/k_sync.c kernel/task/scheduler_service.c; do
                base=${src##*/}
                compile_target \
                    -I kernel -c "$src" -o "build/m97-k-timeouts/$arch-$base.o"
            done
            echo "[M97 TARGET PASS] $arch timed kernel continuations" ;;
        m98)
            compile_target -I kernel \
                -c kernel/task/sched_core.c -o "build/m98-sched-core/$arch.o"
            echo "[M98 TARGET PASS] $arch common scheduler candidate policy" ;;
        m100)
            compile_target -I kernel \
                -c kernel/task/signal.c -o "build/m100-signal/$arch.o"
            echo "[M100 TARGET PASS] $arch fatal signal isolation" ;;
        m101)
            for p in kernel/mm/mm.c kernel/mm/fault.c kernel/mm/usercopy.c kernel/mm/remote_copy.c; do
                out=${p##*/}
                compile_target -I kernel \
                    -c "$p" -o "build/m101-cow/$arch-$out.o"
            done
            echo "[M101 TARGET PASS] $arch private fork, fault, usercopy and ptrace" ;;
        m102)
            compile_target -I kernel \
                -c kernel/drivers/tty.c -o "build/m102-tty-cow/$arch.o"
            compile_target \
                -I scripts/m89-test-include -I kernel \
                -c kernel/fs/file_io.c -o "build/m102-tty-cow/$arch-file-io.o"
            echo "[M102 TARGET PASS] $arch TTY + shared FileOps usercopy/COW adapter" ;;
        m103)
            compile_target \
                -c kernel/task/process.c -o "build/m103-tid/$arch-process.o"
            compile_target \
                -c kernel/task/sched.c -o "build/m103-tid/$arch-sched.o"
            echo "[M103 TARGET PASS] $arch task process + scheduler" ;;
    esac
done
