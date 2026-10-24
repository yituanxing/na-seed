.DEFAULT_GOAL := iso
ifeq ($(ARCH),riscv64)
.DEFAULT_GOAL := port-kernel
ifneq ($(strip $(filter-out port-kernel clean build/deps/limine.h,$(MAKECMDGOALS))),)
$(error riscv64 Makefile port-kernel is M0 smoke only; use sh scripts/build-riscv64-full.sh for the shared userspace kernel)
endif
endif
ifeq ($(ARCH),aarch64)
.DEFAULT_GOAL := port-kernel
ifneq ($(strip $(filter-out port-kernel clean build/deps/limine.h,$(MAKECMDGOALS))),)
$(error aarch64 Makefile port-kernel is M0 smoke only; use sh scripts/build-aarch64-full.sh for the shared userspace kernel)
endif
endif
.DELETE_ON_ERROR:
ARCH ?= x86_64
ifneq ($(filter $(ARCH),x86_64 riscv64 aarch64),$(ARCH))
$(error Unsupported NaSeed ARCH=$(ARCH))
endif
OUT := build
HDR := $(OUT)/deps/limine.h
PROTO_REV := 3a0526b700e356f0eac1b71a77697b3fd1c707a3
BINARY_TAR := $(OUT)/deps/limine-binary.tar.gz
BINARY_EFI := $(OUT)/deps/limine-binary/BOOTX64.EFI
KERNEL := $(OUT)/kernel.elf
ISO := $(OUT)/naseed.iso

CFLAGS := --target=x86_64-unknown-none-elf -std=c11 -O2 -g -Wall -Wextra -Werror \
 -ffreestanding -fno-builtin -fno-stack-protector -fno-stack-check \
 -fno-pic -fno-pie -mcmodel=kernel -mno-red-zone -mno-sse -mno-mmx \
 -I $(OUT)/deps -I kernel
# Opt-in, generic Linux syscall tracing for focused real-program diagnoses.
# Normal kernel and multi-ISA builds are unaffected.
ifeq ($(NAS_SYSCALL_TRACE),1)
CFLAGS += -DNASEED_SYSCALL_TRACE
endif
LDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 \
 -z noexecstack -T kernel/arch/$(ARCH)/linker.ld

.PHONY: all iso kernel smoke run clean bash-iso bash-probe debian-probe debian-bash-probe debian-busybox-probe alpine-fetch alpine-probe alpine-argv-probe alpine-fs-probe alpine-archive-probe alpine-selector-probe ash-diagnostic ash-execve-probe ash-fork-probe ash-pipes-probe ash-lifecycle-probe ash-symlinks-probe ash-tmp-probe ash-tree-probe ash-rename-probe ash-fds-probe ash-paths-probe path-abi-probe ash-pipe-backend-probe boot-handoff-probe ash-resources-probe shebang-abi-probe preflight
all: iso
iso: $(ISO)
kernel: $(KERNEL)

$(HDR):
	mkdir -p $(dir $@)
	curl -fL --retry 3 -o $@.tmp \
	 https://raw.githubusercontent.com/Limine-Bootloader/limine-protocol/$(PROTO_REV)/include/limine.h
	mv $@.tmp $@


# M0-only RV64 hardware smoke. Full RV64 shared-kernel build lives in
# scripts/build-riscv64-full.sh until the target entrypoints are unified.
RVCFLAGS := --target=riscv64-unknown-elf -march=rv64imac -mabi=lp64 \
 -mcmodel=medany -mno-relax -std=c11 -O2 -g -Wall -Wextra -Werror \
 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
 -DNASEED_PORT_M0 -I $(OUT)/deps -I kernel
RVOUT := $(OUT)/port-riscv64
RVKERNEL := $(RVOUT)/kernel.elf

$(RVOUT)/kernel.o: kernel/kernel.c kernel/arch/arch.h kernel/arch/riscv64/cpu.h kernel/arch/riscv64/mmu.h kernel/arch/riscv64/usermode.h $(HDR)
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -MMD -MP -c $< -o $@

$(RVOUT)/frame.o: kernel/mm/frame.c kernel/mm/frame.h $(HDR)
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -MMD -MP -c $< -o $@

$(RVOUT)/cpu.o: kernel/arch/riscv64/cpu.c kernel/arch/riscv64/cpu.h
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -MMD -MP -c $< -o $@

$(RVOUT)/serial.o: kernel/drivers/serial.c kernel/drivers/serial.h kernel/arch/console.h
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -MMD -MP -c $< -o $@

$(RVOUT)/console.o: kernel/arch/riscv64/serial.c kernel/arch/console.h
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -MMD -MP -c $< -o $@

$(RVOUT)/mmu.o: kernel/arch/riscv64/mmu.c kernel/arch/riscv64/mmu.h kernel/mm/frame.h
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -MMD -MP -c $< -o $@

$(RVOUT)/mmu_test.o: kernel/arch/riscv64/mmu_test.c kernel/arch/riscv64/mmu.h
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -MMD -MP -c $< -o $@

$(RVOUT)/user_pt.o: kernel/arch/user_pt.c kernel/arch/user_pt.h
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -MMD -MP -c $< -o $@

$(RVOUT)/user_space.o: kernel/mm/user_space.c kernel/mm/user_space.h
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -MMD -MP -c $< -o $@

$(RVOUT)/user_space_test.o: kernel/mm/user_space_test.c kernel/mm/user_space.h
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -MMD -MP -c $< -o $@

$(RVOUT)/usermode.o: kernel/arch/riscv64/usermode.c kernel/arch/riscv64/usermode.h kernel/arch/riscv64/mmu.h
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -MMD -MP -c $< -o $@

$(RVOUT)/elf_segments.o: kernel/task/elf_segments.c kernel/task/elf_segments.h kernel/task/elf_format.h
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -MMD -MP -c $< -o $@

$(RVOUT)/klib_string.o: kernel/lib/string.c kernel/lib/string.h
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -MMD -MP -c $< -o $@

$(RVOUT)/elf_user.o: kernel/task/elf_user.c kernel/task/elf_user.h kernel/task/elf_segments.h
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -MMD -MP -c $< -o $@
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -MMD -MP -c $< -o $@

$(RVOUT)/user_exit.o: user/riscv64/exit.S
	mkdir -p $(dir $@)
	clang --target=riscv64-unknown-elf -march=rv64imac -mabi=lp64 -mno-relax -c $< -o $@

RVUSER := $(RVOUT)/user-exit.elf
$(RVUSER): $(RVOUT)/user_exit.o user/riscv64/linker.ld
	ld.lld -m elf64lriscv -nostdlib -static --no-relax \
	 -z max-page-size=0x1000 -T user/riscv64/linker.ld $< -o $@

$(RVOUT)/user_entry.o: kernel/arch/riscv64/user_entry.S
	mkdir -p $(dir $@)
	clang $(RVCFLAGS) -c $< -o $@

$(RVKERNEL): $(RVOUT)/kernel.o $(RVOUT)/frame.o $(RVOUT)/cpu.o $(RVOUT)/serial.o $(RVOUT)/console.o $(RVOUT)/user_pt.o $(RVOUT)/user_space.o $(RVOUT)/user_space_test.o $(RVOUT)/mmu.o $(RVOUT)/mmu_test.o $(RVOUT)/usermode.o $(RVOUT)/user_entry.o $(RVOUT)/elf_user.o $(RVOUT)/elf_segments.o $(RVOUT)/klib_string.o kernel/arch/riscv64/linker.ld
	ld.lld -m elf64lriscv -nostdlib -static --no-relax \
	 -z max-page-size=0x1000 -T kernel/arch/riscv64/linker.ld \
	 $(RVOUT)/kernel.o $(RVOUT)/frame.o $(RVOUT)/cpu.o $(RVOUT)/serial.o $(RVOUT)/console.o \
	 $(RVOUT)/user_pt.o $(RVOUT)/user_space.o $(RVOUT)/user_space_test.o $(RVOUT)/mmu.o $(RVOUT)/mmu_test.o $(RVOUT)/usermode.o $(RVOUT)/user_entry.o $(RVOUT)/elf_user.o $(RVOUT)/elf_segments.o $(RVOUT)/klib_string.o -o $@

ifeq ($(ARCH),riscv64)
.PHONY: port-kernel
port-kernel: $(RVKERNEL) $(RVUSER)
	readelf -h $(RVUSER) | grep -q 'Machine:.*RISC-V'
	readelf -h $(RVKERNEL) | grep -q 'Machine:.*RISC-V'
	echo '[PORT] riscv64 real Limine M0 ELF built (QEMU boot not yet verified)'
endif


# Independent actual AArch64 Limine M0 ELF; no pretend syscall/user backend.
ARMCFLAGS := --target=aarch64-none-elf -march=armv8-a -mgeneral-regs-only \
 -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-builtin \
 -fno-stack-protector -fno-pic -fno-pie -fno-unwind-tables \
 -DNASEED_PORT_M0 -I $(OUT)/deps -I kernel
ARMOUT := $(OUT)/port-aarch64
ARMKERNEL := $(ARMOUT)/kernel.elf

$(ARMOUT)/kernel.o: kernel/kernel.c kernel/arch/arch.h kernel/arch/aarch64/cpu.h kernel/arch/aarch64/mmu.h $(HDR)
	mkdir -p $(dir $@)
	clang $(ARMCFLAGS) -MMD -MP -c $< -o $@

$(ARMOUT)/frame.o: kernel/mm/frame.c kernel/mm/frame.h $(HDR)
	mkdir -p $(dir $@)
	clang $(ARMCFLAGS) -MMD -MP -c $< -o $@

$(ARMOUT)/cpu.o: kernel/arch/aarch64/cpu.c kernel/arch/aarch64/cpu.h
	mkdir -p $(dir $@)
	clang $(ARMCFLAGS) -MMD -MP -c $< -o $@

$(ARMOUT)/debug_console.o: kernel/arch/aarch64/debug_console.c kernel/arch/console.h
	mkdir -p $(dir $@)
	clang $(ARMCFLAGS) -MMD -MP -c $< -o $@

$(ARMOUT)/serial.o: kernel/drivers/serial.c kernel/drivers/serial.h kernel/arch/console.h
	mkdir -p $(dir $@)
	clang $(ARMCFLAGS) -MMD -MP -c $< -o $@

$(ARMOUT)/mmu.o: kernel/arch/aarch64/mmu.c kernel/arch/aarch64/mmu.h kernel/mm/frame.h
	mkdir -p $(dir $@)
	clang $(ARMCFLAGS) -MMD -MP -c $< -o $@

$(ARMOUT)/user_pt.o: kernel/arch/user_pt.c kernel/arch/user_pt.h
	mkdir -p $(dir $@)
	clang $(ARMCFLAGS) -MMD -MP -c $< -o $@

$(ARMOUT)/user_space.o: kernel/mm/user_space.c kernel/mm/user_space.h
	mkdir -p $(dir $@)
	clang $(ARMCFLAGS) -MMD -MP -c $< -o $@

$(ARMOUT)/user_space_test.o: kernel/mm/user_space_test.c kernel/mm/user_space.h
	mkdir -p $(dir $@)
	clang $(ARMCFLAGS) -MMD -MP -c $< -o $@

$(ARMOUT)/elf_user.o: kernel/task/elf_user.c kernel/task/elf_user.h kernel/task/elf_segments.h
	mkdir -p $(dir $@)
	clang $(ARMCFLAGS) -MMD -MP -c $< -o $@

$(ARMOUT)/elf_segments.o: kernel/task/elf_segments.c kernel/task/elf_segments.h kernel/task/elf_format.h
	mkdir -p $(dir $@)
	clang $(ARMCFLAGS) -MMD -MP -c $< -o $@

$(ARMOUT)/klib_string.o: kernel/lib/string.c kernel/lib/string.h
	mkdir -p $(dir $@)
	clang $(ARMCFLAGS) -MMD -MP -c $< -o $@

$(ARMOUT)/mmu_test.o: kernel/arch/aarch64/mmu_test.c kernel/arch/aarch64/mmu.h
	mkdir -p $(dir $@)
	clang $(ARMCFLAGS) -MMD -MP -c $< -o $@

$(ARMKERNEL): $(ARMOUT)/kernel.o $(ARMOUT)/frame.o $(ARMOUT)/cpu.o $(ARMOUT)/debug_console.o $(ARMOUT)/serial.o $(ARMOUT)/user_pt.o $(ARMOUT)/user_space.o $(ARMOUT)/user_space_test.o $(ARMOUT)/elf_user.o $(ARMOUT)/elf_segments.o $(ARMOUT)/mmu.o $(ARMOUT)/mmu_test.o $(ARMOUT)/klib_string.o kernel/arch/aarch64/linker.ld
	ld.lld -m aarch64elf -nostdlib -static \
	 -z max-page-size=0x1000 -T kernel/arch/aarch64/linker.ld \
	 $(ARMOUT)/kernel.o $(ARMOUT)/frame.o $(ARMOUT)/cpu.o \
	 $(ARMOUT)/debug_console.o $(ARMOUT)/serial.o $(ARMOUT)/user_pt.o $(ARMOUT)/user_space.o $(ARMOUT)/user_space_test.o $(ARMOUT)/elf_user.o $(ARMOUT)/elf_segments.o $(ARMOUT)/mmu.o $(ARMOUT)/mmu_test.o $(ARMOUT)/klib_string.o -o $@

ifeq ($(ARCH),aarch64)
.PHONY: port-kernel
port-kernel: $(ARMKERNEL)
	readelf -h $(ARMKERNEL) | grep -q 'Machine:.*AArch64'
	echo '[PORT] aarch64 real Limine M0 ELF built (QEMU boot not yet verified)'
endif

$(OUT)/kernel.o: kernel/kernel.c kernel/arch/arch.h kernel/drivers/serial.h kernel/mm/frame.h $(HDR)
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/random.o: kernel/random.c kernel/random.h kernel/mm/usercopy.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/timekeeper.o: kernel/time/clock.c kernel/time/clock.h kernel/arch/arch.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/init_main.o: kernel/init/main.c kernel/init/main.h kernel/panic.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/task_exec.o: kernel/task/exec.c kernel/task/exec.h kernel/task/elf.h kernel/panic.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/syscall_dispatch.o: kernel/syscall/dispatch.c kernel/syscall/dispatch.h kernel/task/exec.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/task.o: kernel/task.c kernel/task.h kernel/fd.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/usercopy.o: kernel/mm/usercopy.c kernel/mm/usercopy.h kernel/arch/mmu.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/elf_segments.o: kernel/task/elf_segments.c kernel/task/elf_segments.h kernel/task/elf_format.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/elf.o: kernel/task/elf.c kernel/task/elf.h kernel/mm/usercopy.h kernel/arch/mmu.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/user_stack.o: kernel/task/user_stack.c kernel/task/user_stack.h kernel/task/elf.h kernel/mm/usercopy.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@


$(OUT)/arch_signal_frame.o: kernel/arch/$(ARCH)/signal_frame.c kernel/task/signal.h kernel/arch/arch.h kernel/arch/$(ARCH)/context.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/signal.o: kernel/task/signal.c kernel/task/signal.h kernel/mm/usercopy.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/klib_string.o: kernel/lib/string.c kernel/lib/string.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/arch_syscall.o: kernel/arch/$(ARCH)/syscall.c kernel/arch/$(ARCH)/syscall.h kernel/mm/usercopy.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_epoll.o: kernel/fs/epoll.c kernel/fs/epoll.h kernel/fd.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_lookup.o: kernel/fs/lookup.c kernel/fs/lookup.h kernel/tmpfs.h kernel/bootfs.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_syscall.o: kernel/fs/syscall.c kernel/fs/syscall.h kernel/fs/io.h kernel/fd.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/session.o: kernel/task/session.c kernel/task/session.h kernel/task.h kernel/errno.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/process.o: kernel/task/process.c kernel/task/process.h kernel/arch/arch.h kernel/arch/mmu.h kernel/mm/usercopy.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/sched.o: kernel/task/sched.c kernel/task/sched.h kernel/task/process.h kernel/fs/io.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/bootfs.o: kernel/bootfs.c kernel/bootfs.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/tmpfs.o: kernel/tmpfs.c kernel/tmpfs.h kernel/fd.h kernel/task.h kernel/bootfs.h kernel/errno.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fd.o: kernel/fd.c kernel/fd.h kernel/task.h kernel/errno.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/path.o: kernel/path.c kernel/path.h kernel/bootfs.h kernel/errno.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/pipe.o: kernel/pipe.c kernel/pipe.h kernel/fd.h kernel/task.h kernel/errno.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/boot_image.o: kernel/boot_image.c kernel/boot_image.h kernel/bootfs.h $(HDR)
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/arch_cpu.o: kernel/arch/$(ARCH)/cpu.c kernel/arch/$(ARCH)/cpu.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/arch_mmu.o: kernel/arch/$(ARCH)/mmu.c kernel/arch/$(ARCH)/mmu.h kernel/task.h kernel/mm/frame.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/vm.o: kernel/mm/vm.c kernel/mm/vm.h kernel/mm/anon_regions.h kernel/arch/mmu.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/anon_regions.o: kernel/mm/anon_regions.c kernel/mm/anon_regions.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/arch_context.o: kernel/arch/$(ARCH)/context.c kernel/arch/$(ARCH)/context.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/arch_timer.o: kernel/arch/$(ARCH)/timer.c kernel/arch/$(ARCH)/irq.h kernel/task/sched.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/tty_ldisc.o: kernel/drivers/tty_ldisc.c kernel/drivers/tty_ldisc.h kernel/drivers/tty.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/tty.o: kernel/drivers/tty.c kernel/drivers/tty.h kernel/drivers/tty_ldisc.h kernel/arch/console.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/serial.o: kernel/drivers/serial.c kernel/drivers/serial.h kernel/arch/console.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/frame.o: kernel/mm/frame.c kernel/mm/frame.h $(HDR)
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/arch_console.o: kernel/arch/$(ARCH)/console.c kernel/arch/console.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/platform.o: kernel/platform/qemu_debug_exit.c kernel/platform/platform.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/arch.o: kernel/arch/$(ARCH)/entry.S
	mkdir -p $(dir $@)
	clang --target=x86_64-unknown-none-elf -g -mno-red-zone -c $< -o $@

$(OUT)/user-hello.o: user/hello.S kernel/nr.h kernel/errno.h
	mkdir -p $(dir $@)
	clang --target=x86_64-unknown-none-elf -g -c $< -o $@

$(OUT)/user-hello: $(OUT)/user-hello.o user/linker.ld
	ld.lld -m elf_x86_64 -nostdlib -static -T user/linker.ld $< -o $@

$(OUT)/path-abi.o: user/path-abi.S kernel/nr.h kernel/errno.h
	mkdir -p $(dir $@)
	clang --target=x86_64-unknown-none-elf -g -c $< -o $@

$(OUT)/path-abi: $(OUT)/path-abi.o user/linker.ld
	ld.lld -m elf_x86_64 -nostdlib -static -T user/linker.ld $< -o $@

$(OUT)/shebang-abi.o: user/shebang-abi.S kernel/nr.h kernel/errno.h
	mkdir -p $(dir $@)
	clang --target=x86_64-unknown-none-elf -g -c $< -o $@

$(OUT)/shebang-abi: $(OUT)/shebang-abi.o user/linker.ld
	ld.lld -m elf_x86_64 -nostdlib -static -T user/linker.ld $< -o $@

$(KERNEL): $(OUT)/kernel.o $(OUT)/random.o $(OUT)/timekeeper.o $(OUT)/init_main.o $(OUT)/task_exec.o $(OUT)/syscall_dispatch.o $(OUT)/task.o $(OUT)/process.o $(OUT)/session.o $(OUT)/sched.o $(OUT)/bootfs.o $(OUT)/tmpfs.o $(OUT)/fd.o $(OUT)/path.o $(OUT)/pipe.o $(OUT)/boot_image.o $(OUT)/arch.o $(OUT)/arch_cpu.o $(OUT)/arch_context.o $(OUT)/arch_timer.o $(OUT)/serial.o $(OUT)/tty.o $(OUT)/tty_ldisc.o $(OUT)/arch_console.o $(OUT)/frame.o $(OUT)/platform.o $(OUT)/arch_mmu.o $(OUT)/vm.o $(OUT)/anon_regions.o $(OUT)/usercopy.o $(OUT)/elf.o $(OUT)/elf_segments.o $(OUT)/user_stack.o $(OUT)/signal.o $(OUT)/arch_signal_frame.o $(OUT)/klib_string.o $(OUT)/arch_syscall.o $(OUT)/fs_lookup.o $(OUT)/fs_epoll.o $(OUT)/fs_syscall.o kernel/arch/$(ARCH)/linker.ld
	ld.lld $(LDFLAGS) $(OUT)/kernel.o $(OUT)/random.o $(OUT)/timekeeper.o $(OUT)/init_main.o $(OUT)/task_exec.o $(OUT)/syscall_dispatch.o $(OUT)/task.o $(OUT)/process.o $(OUT)/session.o $(OUT)/sched.o $(OUT)/bootfs.o $(OUT)/tmpfs.o $(OUT)/fd.o $(OUT)/path.o $(OUT)/pipe.o $(OUT)/boot_image.o $(OUT)/arch.o $(OUT)/arch_cpu.o $(OUT)/arch_context.o $(OUT)/arch_timer.o $(OUT)/serial.o $(OUT)/tty.o $(OUT)/tty_ldisc.o $(OUT)/arch_console.o $(OUT)/frame.o $(OUT)/platform.o $(OUT)/arch_mmu.o $(OUT)/vm.o $(OUT)/anon_regions.o $(OUT)/usercopy.o $(OUT)/elf.o $(OUT)/elf_segments.o $(OUT)/user_stack.o $(OUT)/signal.o $(OUT)/arch_signal_frame.o $(OUT)/klib_string.o $(OUT)/arch_syscall.o $(OUT)/fs_lookup.o $(OUT)/fs_epoll.o $(OUT)/fs_syscall.o -o $@

$(BINARY_TAR):
	mkdir -p $(dir $@)
	curl -fL --retry 3 -o $@.tmp \
	 https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz
	mv $@.tmp $@

$(BINARY_EFI): $(BINARY_TAR)
	tar -xzf $(BINARY_TAR) -C $(OUT)/deps
	test -f $@

$(ISO): $(KERNEL) $(OUT)/user-hello $(BINARY_EFI) limine.conf
	rm -rf $(OUT)/iso
	mkdir -p $(OUT)/iso/boot/limine $(OUT)/iso/EFI/BOOT
	cp $(KERNEL) $(OUT)/iso/boot/kernel
	cp $(OUT)/user-hello $(OUT)/iso/boot/user-hello
	cp limine.conf $(OUT)/iso/boot/limine/limine.conf
	cp $(OUT)/deps/limine-binary/limine-uefi-cd.bin $(OUT)/iso/boot/limine/
	cp $(BINARY_EFI) $(OUT)/iso/EFI/BOOT/BOOTX64.EFI
	xorriso -as mkisofs -R -r -J \
	 --efi-boot boot/limine/limine-uefi-cd.bin \
	 --efi-boot-part --efi-boot-image \
	 -o $(ISO) $(OUT)/iso


BASH_BIN := $(OUT)/userspace/bash
BASH_ISO := $(OUT)/naseed-bash.iso

# CI restores Bash from a content-addressed cache keyed by build-bash.sh.
# Order-only dependency prevents checkout's newer script mtime from forcing
# a full rebuild of an already-verified cached executable every run.
# When changing this builder locally, delete $(BASH_BIN) or use make -B.
$(BASH_BIN): | scripts/build-bash.sh
	sh scripts/build-bash.sh "$(abspath $(OUT))"
	test -x "$@"

$(BASH_ISO): $(KERNEL) $(BASH_BIN) $(BINARY_EFI) limine-bash.conf
	rm -rf $(OUT)/bash-iso
	mkdir -p $(OUT)/bash-iso/boot/limine $(OUT)/bash-iso/EFI/BOOT
	cp $(KERNEL) $(OUT)/bash-iso/boot/kernel
	cp $(BASH_BIN) $(OUT)/bash-iso/boot/bash
	cp limine-bash.conf $(OUT)/bash-iso/boot/limine/limine.conf
	cp $(OUT)/deps/limine-binary/limine-uefi-cd.bin $(OUT)/bash-iso/boot/limine/
	cp $(BINARY_EFI) $(OUT)/bash-iso/EFI/BOOT/BOOTX64.EFI
	xorriso -as mkisofs -R -r -J \
	 --efi-boot boot/limine/limine-uefi-cd.bin \
	 --efi-boot-part --efi-boot-image -o $@ $(OUT)/bash-iso

bash-iso: $(BASH_ISO)
bash-probe: $(BASH_ISO)
	sh scripts/probe-bash.sh $(BASH_ISO)


# Debian 13 binary packages, SHA256 verified in scripts/fetch-debian.sh.
DEBIAN_BASH := $(OUT)/userspace/debian-bash
DEBIAN_BUSYBOX := $(OUT)/userspace/debian-busybox
DEBIAN_BASH_ISO := $(OUT)/naseed-debian-bash.iso
DEBIAN_BUSYBOX_ISO := $(OUT)/naseed-debian-busybox.iso

$(OUT)/userspace/debian-%: scripts/fetch-debian.sh
	sh scripts/fetch-debian.sh $* "$(abspath $(OUT))"

$(DEBIAN_BASH_ISO): $(KERNEL) $(DEBIAN_BASH) $(BINARY_EFI) limine-bash.conf
	rm -rf $(OUT)/debian-bash-iso
	mkdir -p $(OUT)/debian-bash-iso/boot/limine $(OUT)/debian-bash-iso/EFI/BOOT
	cp $(KERNEL) $(OUT)/debian-bash-iso/boot/kernel
	cp $(DEBIAN_BASH) $(OUT)/debian-bash-iso/boot/bash
	cp limine-bash.conf $(OUT)/debian-bash-iso/boot/limine/limine.conf
	cp $(OUT)/deps/limine-binary/limine-uefi-cd.bin $(OUT)/debian-bash-iso/boot/limine/
	cp $(BINARY_EFI) $(OUT)/debian-bash-iso/EFI/BOOT/BOOTX64.EFI
	xorriso -as mkisofs -R -r -J \
	  --efi-boot boot/limine/limine-uefi-cd.bin \
	  --efi-boot-part --efi-boot-image -o $@ $(OUT)/debian-bash-iso

$(DEBIAN_BUSYBOX_ISO): $(KERNEL) $(DEBIAN_BUSYBOX) $(BINARY_EFI) limine-busybox.conf
	rm -rf $(OUT)/debian-busybox-iso
	mkdir -p $(OUT)/debian-busybox-iso/boot/limine $(OUT)/debian-busybox-iso/EFI/BOOT
	cp $(KERNEL) $(OUT)/debian-busybox-iso/boot/kernel
	cp $(DEBIAN_BUSYBOX) $(OUT)/debian-busybox-iso/boot/busybox
	cp limine-busybox.conf $(OUT)/debian-busybox-iso/boot/limine/limine.conf
	cp $(OUT)/deps/limine-binary/limine-uefi-cd.bin $(OUT)/debian-busybox-iso/boot/limine/
	cp $(BINARY_EFI) $(OUT)/debian-busybox-iso/EFI/BOOT/BOOTX64.EFI
	xorriso -as mkisofs -R -r -J \
	  --efi-boot boot/limine/limine-uefi-cd.bin \
	  --efi-boot-part --efi-boot-image -o $@ $(OUT)/debian-busybox-iso

debian-bash-probe: $(DEBIAN_BASH_ISO)
	sh scripts/probe-debian.sh bash $(DEBIAN_BASH_ISO)
debian-busybox-probe: $(DEBIAN_BUSYBOX_ISO)
	sh scripts/probe-debian.sh busybox $(DEBIAN_BUSYBOX_ISO)
debian-probe: debian-bash-probe debian-busybox-probe


# Official Alpine: stage real ET_DYN interpreter and dynamic BusyBox first.
$(OUT)/alpine/bin/busybox $(OUT)/alpine/lib/ld-musl-x86_64.so.1 &: scripts/fetch-alpine.sh
	sh scripts/fetch-alpine.sh "$(abspath $(OUT))"
alpine-fetch: $(OUT)/alpine/bin/busybox $(OUT)/alpine/lib/ld-musl-x86_64.so.1


ALPINE_ISO := $(OUT)/naseed-alpine-dynamic.iso
$(ALPINE_ISO): $(KERNEL) $(OUT)/alpine/bin/busybox $(OUT)/alpine/lib/ld-musl-x86_64.so.1 $(BINARY_EFI) limine-alpine.conf
	rm -rf $(OUT)/alpine-iso
	mkdir -p $(OUT)/alpine-iso/boot/limine $(OUT)/alpine-iso/EFI/BOOT
	cp $(KERNEL) $(OUT)/alpine-iso/boot/kernel
	cp $(OUT)/alpine/bin/busybox $(OUT)/alpine-iso/boot/busybox
	cp $(OUT)/alpine/lib/ld-musl-x86_64.so.1 $(OUT)/alpine-iso/boot/ld-musl-x86_64.so.1
	cp limine-alpine.conf $(OUT)/alpine-iso/boot/limine/limine.conf
	cp $(OUT)/deps/limine-binary/limine-uefi-cd.bin $(OUT)/alpine-iso/boot/limine/
	cp $(BINARY_EFI) $(OUT)/alpine-iso/EFI/BOOT/BOOTX64.EFI
	xorriso -as mkisofs -R -r -J \
	  --efi-boot boot/limine/limine-uefi-cd.bin \
	  --efi-boot-part --efi-boot-image -o $@ $(OUT)/alpine-iso

alpine-probe: $(ALPINE_ISO)
	sh scripts/probe-alpine.sh $(ALPINE_ISO)

# Reuse exactly the same prebuilt Alpine binary. Change only Limine cmdline,
# not kernel code, to test multiple real BusyBox applets.
alpine-argv-probe: $(ALPINE_ISO)
	sh scripts/probe-alpine-argv.sh

# Stage a REAL read-only CPIO newc archive as a third Limine module.
# The legacy module ELF path remains unchanged to preserve M4 regression.
ALPINE_FS_IMAGE := $(OUT)/alpine-rootfs.cpio
ALPINE_FS_ISO := $(OUT)/naseed-alpine-fs.iso

$(ALPINE_FS_IMAGE): $(OUT)/alpine/bin/busybox $(OUT)/alpine/lib/ld-musl-x86_64.so.1 scripts/make-initramfs.sh
	sh scripts/make-initramfs.sh "$@"

$(ALPINE_FS_ISO): $(KERNEL) $(ALPINE_FS_IMAGE) $(BINARY_EFI) limine-alpine-fs.conf
	rm -rf $(OUT)/alpine-fs-iso
	mkdir -p $(OUT)/alpine-fs-iso/boot/limine $(OUT)/alpine-fs-iso/EFI/BOOT
	cp $(KERNEL) $(OUT)/alpine-fs-iso/boot/kernel
	cp $(OUT)/alpine/bin/busybox $(OUT)/alpine-fs-iso/boot/busybox
	cp $(OUT)/alpine/lib/ld-musl-x86_64.so.1 $(OUT)/alpine-fs-iso/boot/ld-musl-x86_64.so.1
	cp $(ALPINE_FS_IMAGE) $(OUT)/alpine-fs-iso/boot/rootfs.cpio
	cp limine-alpine-fs.conf $(OUT)/alpine-fs-iso/boot/limine/limine.conf
	cp $(OUT)/deps/limine-binary/limine-uefi-cd.bin $(OUT)/alpine-fs-iso/boot/limine/
	cp $(BINARY_EFI) $(OUT)/alpine-fs-iso/EFI/BOOT/BOOTX64.EFI
	xorriso -as mkisofs -R -r -J \
	 --efi-boot boot/limine/limine-uefi-cd.bin \
	 --efi-boot-part --efi-boot-image -o $@ $(OUT)/alpine-fs-iso

alpine-fs-probe: $(ALPINE_FS_ISO)
	sh scripts/probe-alpine-fs.sh

# Archive-only boot: no separately staged BusyBox/musl modules. The kernel
# fetches both actual ELF images by path from the indexed CPIO contents.
ALPINE_ARCHIVE_ISO := $(OUT)/naseed-alpine-archive.iso
$(ALPINE_ARCHIVE_ISO): $(KERNEL) $(ALPINE_FS_IMAGE) $(BINARY_EFI) limine-alpine-archive.conf
	rm -rf $(OUT)/alpine-archive-iso
	mkdir -p $(OUT)/alpine-archive-iso/boot/limine $(OUT)/alpine-archive-iso/EFI/BOOT
	cp $(KERNEL) $(OUT)/alpine-archive-iso/boot/kernel
	cp $(ALPINE_FS_IMAGE) $(OUT)/alpine-archive-iso/boot/rootfs.cpio
	cp limine-alpine-archive.conf $(OUT)/alpine-archive-iso/boot/limine/limine.conf
	cp $(OUT)/deps/limine-binary/limine-uefi-cd.bin $(OUT)/alpine-archive-iso/boot/limine/
	cp $(BINARY_EFI) $(OUT)/alpine-archive-iso/EFI/BOOT/BOOTX64.EFI
	xorriso -as mkisofs -R -r -J \
	 --efi-boot boot/limine/limine-uefi-cd.bin \
	 --efi-boot-part --efi-boot-image -o $@ $(OUT)/alpine-archive-iso

alpine-archive-probe: $(ALPINE_ARCHIVE_ISO)
	NASEED_FS_BASE=build/alpine-archive-iso \
	 NASEED_FS_CONFIG=limine-alpine-archive.conf \
	 NASEED_FS_PREFIX=alpine-archive sh scripts/probe-alpine-fs.sh

# Two independent, upstream, checksum-pinned BusyBox ELFs in one CPIO:
# Alpine dynamic musl at /bin/busybox and Debian static at /usr/bin/busybox.
# Select by Limine argv[0] only; kernel remains unchanged between boots.
ALPINE_SELECTOR_IMAGE := $(OUT)/alpine-selector-rootfs.cpio
$(ALPINE_SELECTOR_IMAGE): $(OUT)/alpine/bin/busybox $(OUT)/alpine/lib/ld-musl-x86_64.so.1 $(DEBIAN_BUSYBOX) scripts/make-initramfs.sh
	NASEED_SECOND_BUSYBOX=$(DEBIAN_BUSYBOX) sh scripts/make-initramfs.sh "$@"

alpine-selector-probe: $(ALPINE_ARCHIVE_ISO) $(ALPINE_SELECTOR_IMAGE)
	sh scripts/probe-alpine-selector.sh

ash-diagnostic: $(ALPINE_ARCHIVE_ISO)
	sh scripts/probe-ash-diagnostic.sh

ash-execve-probe: $(ALPINE_ARCHIVE_ISO)
	sh scripts/probe-ash-execve.sh

ash-fork-probe: $(ALPINE_ARCHIVE_ISO)
	sh scripts/probe-ash-fork.sh

ash-pipes-probe: $(ALPINE_ARCHIVE_ISO)
	sh scripts/probe-ash-pipes.sh

ash-pipe-backend-probe: $(ALPINE_ARCHIVE_ISO)
	sh scripts/probe-ash-pipe-backend.sh

boot-handoff-probe: $(ALPINE_ARCHIVE_ISO)
	sh scripts/probe-boot-handoff.sh

ash-resources-probe: $(ALPINE_ARCHIVE_ISO)
	sh scripts/probe-ash-resources.sh

ash-lifecycle-probe: $(ALPINE_ARCHIVE_ISO)
	sh scripts/probe-ash-lifecycle.sh

ash-symlinks-probe: $(ALPINE_ARCHIVE_ISO)
	sh scripts/probe-ash-symlinks.sh

ash-tmp-probe: $(ALPINE_ARCHIVE_ISO)
	sh scripts/probe-ash-tmp.sh

ash-tree-probe: $(ALPINE_ARCHIVE_ISO)
	sh scripts/probe-ash-tree.sh

ash-rename-probe: $(ALPINE_ARCHIVE_ISO)
	sh scripts/probe-ash-rename.sh

ash-fds-probe: $(ALPINE_ARCHIVE_ISO)
	sh scripts/probe-ash-fds.sh

ash-paths-probe: $(ALPINE_ARCHIVE_ISO)
	sh scripts/probe-ash-paths.sh

# Optional direct Linux ABI probe, additional to (not in place of) the
# unchanged BusyBox QEMU regression. Stage a dedicated CPIO ISO fixture.
PATH_ABI_IMAGE := $(OUT)/path-abi-rootfs.cpio
PATH_ABI_ISO := $(OUT)/naseed-path-abi.iso
$(PATH_ABI_IMAGE): $(OUT)/path-abi $(OUT)/alpine/bin/busybox $(OUT)/alpine/lib/ld-musl-x86_64.so.1 scripts/make-initramfs.sh
	NASEED_PATH_ABI=$(abspath $(OUT)/path-abi) sh scripts/make-initramfs.sh "$@"

$(PATH_ABI_ISO): $(ALPINE_ARCHIVE_ISO) $(PATH_ABI_IMAGE)
	rm -rf $(OUT)/path-abi-iso
	cp -R $(OUT)/alpine-archive-iso $(OUT)/path-abi-iso
	cp $(PATH_ABI_IMAGE) $(OUT)/path-abi-iso/boot/rootfs.cpio
	awk '/^    cmdline: / { print "    cmdline: naseed.argv=/bin/path-abi"; next } { print }' limine-alpine-archive.conf > $(OUT)/path-abi-iso/boot/limine/limine.conf
	xorriso -as mkisofs -R -r -J \
	 --efi-boot boot/limine/limine-uefi-cd.bin \
	 --efi-boot-part --efi-boot-image -o $@ $(OUT)/path-abi-iso

path-abi-probe: $(PATH_ABI_ISO)
	sh scripts/probe-path-abi.sh

# M8.0 original executable CPIO scripts, invoked via actual Linux
# execve(2), *not* as arguments to a kernel-owned command interpreter.
SHEBANG_ABI_IMAGE := $(OUT)/shebang-abi-rootfs.cpio
$(SHEBANG_ABI_IMAGE): $(OUT)/shebang-abi $(OUT)/alpine/bin/busybox $(OUT)/alpine/lib/ld-musl-x86_64.so.1 scripts/make-initramfs.sh
	NASEED_SHEBANG_ABI=$(abspath $(OUT)/shebang-abi) sh scripts/make-initramfs.sh "$@"

shebang-abi-probe: $(ALPINE_ARCHIVE_ISO) $(SHEBANG_ABI_IMAGE)
	sh scripts/probe-shebang.sh

preflight:
	sh scripts/preflight.sh

smoke: $(ISO)
	sh scripts/smoke.sh $(ISO)
run: $(ISO)
	sh scripts/run.sh $(ISO)
clean:
	rm -rf $(OUT)

-include $(OUT)/session.d $(OUT)/kernel.d $(OUT)/m1.d $(OUT)/bootfs.d $(OUT)/fd.d $(OUT)/path.d $(OUT)/pipe.d $(OUT)/boot_image.d $(OUT)/task.d
