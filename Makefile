.DEFAULT_GOAL := iso
.DELETE_ON_ERROR:
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
 -I $(OUT)/deps
LDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 \
 -z noexecstack -T kernel/linker.ld

.PHONY: all iso kernel smoke run clean
all: iso
iso: $(ISO)
kernel: $(KERNEL)

$(HDR):
	mkdir -p $(dir $@)
	curl -fL --retry 3 -o $@.tmp \
	 https://raw.githubusercontent.com/Limine-Bootloader/limine-protocol/$(PROTO_REV)/include/limine.h
	mv $@.tmp $@

$(OUT)/kernel.o: kernel/kernel.c $(HDR)
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(KERNEL): $(OUT)/kernel.o kernel/linker.ld
	ld.lld $(LDFLAGS) $(OUT)/kernel.o -o $@

$(BINARY_TAR):
	mkdir -p $(dir $@)
	curl -fL --retry 3 -o $@.tmp \
	 https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz
	mv $@.tmp $@

$(BINARY_EFI): $(BINARY_TAR)
	tar -xzf $(BINARY_TAR) -C $(OUT)/deps
	test -f $@

$(ISO): $(KERNEL) $(BINARY_EFI) limine.conf
	rm -rf $(OUT)/iso
	mkdir -p $(OUT)/iso/boot/limine $(OUT)/iso/EFI/BOOT
	cp $(KERNEL) $(OUT)/iso/boot/kernel
	cp limine.conf $(OUT)/iso/boot/limine/limine.conf
	cp $(OUT)/deps/limine-binary/limine-uefi-cd.bin $(OUT)/iso/boot/limine/
	cp $(BINARY_EFI) $(OUT)/iso/EFI/BOOT/BOOTX64.EFI
	xorriso -as mkisofs -R -r -J \
	 --efi-boot boot/limine/limine-uefi-cd.bin \
	 --efi-boot-part --efi-boot-image \
	 -o $(ISO) $(OUT)/iso

smoke: $(ISO)
	sh scripts/smoke.sh $(ISO)
run: $(ISO)
	sh scripts/run.sh $(ISO)
clean:
	rm -rf $(OUT)

-include $(OUT)/kernel.d
