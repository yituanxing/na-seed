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

.PHONY: all iso kernel smoke run clean bash-iso bash-probe debian-probe debian-bash-probe debian-busybox-probe alpine-fetch alpine-probe alpine-argv-probe preflight
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

$(OUT)/m1.o: kernel/m1.c kernel/nr.h kernel/errno.h $(HDR)
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/arch.o: kernel/arch.S
	mkdir -p $(dir $@)
	clang --target=x86_64-unknown-none-elf -g -mno-red-zone -c $< -o $@

$(OUT)/user-hello.o: user/hello.S kernel/nr.h kernel/errno.h
	mkdir -p $(dir $@)
	clang --target=x86_64-unknown-none-elf -g -c $< -o $@

$(OUT)/user-hello: $(OUT)/user-hello.o user/linker.ld
	ld.lld -m elf_x86_64 -nostdlib -static -T user/linker.ld $< -o $@

$(KERNEL): $(OUT)/kernel.o $(OUT)/m1.o $(OUT)/arch.o kernel/linker.ld
	ld.lld $(LDFLAGS) $(OUT)/kernel.o $(OUT)/m1.o $(OUT)/arch.o -o $@

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

preflight:
	sh scripts/preflight.sh

smoke: $(ISO)
	sh scripts/smoke.sh $(ISO)
run: $(ISO)
	sh scripts/run.sh $(ISO)
clean:
	rm -rf $(OUT)

-include $(OUT)/kernel.d $(OUT)/m1.d
