ARCH ?= x86_64
.DEFAULT_GOAL := iso
ifneq ($(ARCH),x86_64)
.DEFAULT_GOAL := kernel
endif
.DELETE_ON_ERROR:
NAS_ENABLE_LWIP ?= 1
ifneq ($(NAS_ENABLE_LWIP),1)
$(error NaSeed AF_INET requires the single upstream lwIP provider; NAS_ENABLE_LWIP=0 is retired)
endif
ifneq ($(filter $(ARCH),x86_64 riscv64 aarch64),$(ARCH))
$(error Unsupported NaSeed ARCH=$(ARCH))
endif
OUT ?= build
HDR := $(OUT)/deps/limine.h
PROTO_REV := 3a0526b700e356f0eac1b71a77697b3fd1c707a3
BINARY_TAR := $(OUT)/deps/limine-binary.tar.gz
BINARY_EFI := $(OUT)/deps/limine-binary/BOOTX64.EFI
KERNEL := $(OUT)/kernel.elf
ISO := $(OUT)/naseed.iso

CFLAGS := --target=x86_64-unknown-none-elf -std=c11 -O2 -g -Wall -Wextra -Werror \
 -ffreestanding -fno-builtin -fno-stack-protector -fno-stack-check \
 -fno-pic -fno-pie -mcmodel=kernel -mno-red-zone -mno-sse -mno-mmx \
 -I $(OUT)/deps -I kernel -I third_party/unikraft/include -I ports/unikraft/include
# Physical-machine H0 is a separate read-only firmware/PCI discovery image.
# It deliberately stops before userspace, VirtIO, DMA, IRQ setup or disk I/O.
ifeq ($(NAS_ENABLE_H0_HARDWARE_DISCOVERY),1)
ifneq ($(ARCH),x86_64)
$(error NaSeed H0 hardware discovery is currently x86-64 only)
endif
CFLAGS += -DNAS_ENABLE_H0_HARDWARE_DISCOVERY=1
endif
ifeq ($(NAS_ENABLE_H1_ECAM_DISCOVERY),1)
ifneq ($(NAS_ENABLE_H0_HARDWARE_DISCOVERY),1)
$(error NAS_ENABLE_H1_ECAM_DISCOVERY requires the read-only H0 discovery profile)
endif
CFLAGS += -DNAS_ENABLE_H1_ECAM_DISCOVERY=1
endif
ifeq ($(NAS_ENABLE_H2_PCI_RESOURCE_INVENTORY),1)
ifneq ($(NAS_ENABLE_H1_ECAM_DISCOVERY),1)
$(error NAS_ENABLE_H2_PCI_RESOURCE_INVENTORY requires the read-only H1 ECAM profile)
endif
CFLAGS += -DNAS_ENABLE_H2_PCI_RESOURCE_INVENTORY=1
endif
ifeq ($(NAS_ENABLE_H3_QEMU_NVME_IDENTIFY),1)
ifneq ($(NAS_ENABLE_H2_PCI_RESOURCE_INVENTORY),1)
$(error NAS_ENABLE_H3_QEMU_NVME_IDENTIFY requires H2 discovery)
endif
CFLAGS += -DNAS_ENABLE_H3_QEMU_NVME_IDENTIFY=1
endif
ifeq ($(NAS_ENABLE_H4_QEMU_NVME_READONLY),1)
ifneq ($(NAS_ENABLE_H3_QEMU_NVME_IDENTIFY),1)
$(error NAS_ENABLE_H4_QEMU_NVME_READONLY requires isolated H3 QEMU NVMe profile)
endif
CFLAGS += -DNAS_ENABLE_H4_QEMU_NVME_READONLY=1
endif
ifeq ($(NAS_ENABLE_H5_QEMU_PCI_RESOURCE),1)
ifneq ($(NAS_ENABLE_H4_QEMU_NVME_READONLY),1)
$(error NAS_ENABLE_H5_QEMU_PCI_RESOURCE requires the isolated H4 read-only namespace profile)
endif
CFLAGS += -DNAS_ENABLE_H5_QEMU_PCI_RESOURCE=1
endif
ifeq ($(NAS_ENABLE_H6_QEMU_RESOURCE_LIFETIME),1)
ifneq ($(NAS_ENABLE_H5_QEMU_PCI_RESOURCE),1)
$(error NAS_ENABLE_H6_QEMU_RESOURCE_LIFETIME requires isolated H5 QEMU PCI resource profile)
endif
CFLAGS += -DNAS_ENABLE_H6_QEMU_RESOURCE_LIFETIME=1
endif
ifeq ($(NAS_ENABLE_H6_QEMU_AMBIGUOUS_NVME),1)
ifneq ($(NAS_ENABLE_H6_QEMU_RESOURCE_LIFETIME),1)
$(error NAS_ENABLE_H6_QEMU_AMBIGUOUS_NVME requires H6 QEMU-only resource profile)
endif
CFLAGS += -DNAS_ENABLE_H6_QEMU_AMBIGUOUS_NVME=1
endif
ifeq ($(NAS_ENABLE_H7_QEMU_NVME_USERLAND),1)
ifneq ($(NAS_ENABLE_H6_QEMU_RESOURCE_LIFETIME)$(NAS_ENABLE_LWEXT4),11)
$(error H7 NVMe userland requires NAS_ENABLE_H6_QEMU_RESOURCE_LIFETIME=1 NAS_ENABLE_LWEXT4=1)
endif
CFLAGS += -DNAS_ENABLE_H7_QEMU_NVME_USERLAND=1
endif
ifeq ($(NAS_ENABLE_H8_QEMU_ALPINE_ROOT),1)
ifneq ($(NAS_ENABLE_H7_QEMU_NVME_USERLAND)$(NAS_ENABLE_LWEXT4),11)
$(error H8 original Alpine NVMe root requires H7 QEMU NVMe userland and lwext4)
endif
CFLAGS += -DNAS_ENABLE_H8_QEMU_ALPINE_ROOT=1
endif
# H9 is a separate QEMU-only ICH9 AHCI/SATA software-boot target. It uses
# the same VFS and original Alpine userland as H8, but no NVMe driver at all.
ifeq ($(NAS_ENABLE_H9_QEMU_AHCI_USERLAND),1)
ifneq ($(NAS_ENABLE_H0_HARDWARE_DISCOVERY)$(NAS_ENABLE_H1_ECAM_DISCOVERY)$(NAS_ENABLE_H2_PCI_RESOURCE_INVENTORY)$(NAS_ENABLE_LWEXT4),1111)
$(error H9 AHCI userland requires H0/H1/H2 PCI discovery and lwext4)
endif
ifneq ($(filter 1,$(NAS_ENABLE_H3_QEMU_NVME_IDENTIFY) $(NAS_ENABLE_H4_QEMU_NVME_READONLY) $(NAS_ENABLE_H5_QEMU_PCI_RESOURCE) $(NAS_ENABLE_H6_QEMU_RESOURCE_LIFETIME) $(NAS_ENABLE_H7_QEMU_NVME_USERLAND) $(NAS_ENABLE_H8_QEMU_ALPINE_ROOT) $(NAS_ENABLE_VIRTIO_BLK)),)
$(error H9 QEMU AHCI and NVMe/VirtIO block profiles are mutually exclusive)
endif
CFLAGS += -DNAS_ENABLE_H9_QEMU_AHCI_USERLAND=1
endif
# H10 keeps H8's already proven signed Alpine/NVMe root as a control and
# changes ONLY the network controller to a QEMU-only Intel E1000 PCI NIC.
ifeq ($(NAS_ENABLE_H10_QEMU_E1000_USERLAND),1)
ifneq ($(NAS_ENABLE_H8_QEMU_ALPINE_ROOT)$(NAS_ENABLE_H2_PCI_RESOURCE_INVENTORY)$(NAS_ENABLE_LWEXT4),111)
$(error H10 E1000 requires H8 original Alpine ext4 / and H2 PCI resource discovery)
endif
ifneq ($(filter 1,$(NAS_ENABLE_H9_QEMU_AHCI_USERLAND) $(NAS_ENABLE_VIRTIO_NET)),)
$(error H10 E1000 cannot share the H9 AHCI/VirtIO network profile)
endif
CFLAGS += -DNAS_ENABLE_H10_QEMU_E1000_USERLAND=1
endif
# H11 isolates one explicit QEMU xHCI PCI controller and USB Mass Storage
# read-only Alpine root. It must never inherit another block-controller path.
ifeq ($(NAS_ENABLE_H11_QEMU_XHCI_USERLAND),1)
ifneq ($(NAS_ENABLE_H0_HARDWARE_DISCOVERY)$(NAS_ENABLE_H1_ECAM_DISCOVERY)$(NAS_ENABLE_H2_PCI_RESOURCE_INVENTORY)$(NAS_ENABLE_LWEXT4),1111)
$(error H11 USB original Alpine requires H0/H1/H2 discovery and lwext4)
endif
ifneq ($(filter 1,$(NAS_ENABLE_H3_QEMU_NVME_IDENTIFY) $(NAS_ENABLE_H4_QEMU_NVME_READONLY) $(NAS_ENABLE_H5_QEMU_PCI_RESOURCE) $(NAS_ENABLE_H6_QEMU_RESOURCE_LIFETIME) $(NAS_ENABLE_H7_QEMU_NVME_USERLAND) $(NAS_ENABLE_H8_QEMU_ALPINE_ROOT) $(NAS_ENABLE_H9_QEMU_AHCI_USERLAND) $(NAS_ENABLE_H10_QEMU_E1000_USERLAND) $(NAS_ENABLE_VIRTIO_BLK)),)
$(error H11 QEMU xHCI USB root is exclusive of NVMe/AHCI/VirtIO block profiles)
endif
CFLAGS += -DNAS_ENABLE_H11_QEMU_XHCI_USERLAND=1
endif
# H12 reuses the H8 signed Alpine/NVMe root with one isolated QEMU xHCI
# USB Boot Keyboard. The Linux input core/evdev is provider-neutral: its
# H12 object linkage must NOT implicitly compile a VirtIO input backend.
ifeq ($(NAS_ENABLE_H12_QEMU_USB_HID_USERLAND),1)
ifneq ($(NAS_ENABLE_H8_QEMU_ALPINE_ROOT)$(NAS_ENABLE_H2_PCI_RESOURCE_INVENTORY)$(NAS_ENABLE_LWEXT4),111)
$(error H12 USB HID requires H8 original Alpine ext4 root, H2 PCI discovery and lwext4)
endif
ifneq ($(filter 1,$(NAS_ENABLE_H9_QEMU_AHCI_USERLAND) $(NAS_ENABLE_H10_QEMU_E1000_USERLAND) $(NAS_ENABLE_H11_QEMU_XHCI_USERLAND) $(NAS_ENABLE_VIRTIO_INPUT)),)
$(error H12 isolated USB HID cannot share H9/H10/H11 or VirtIO Input profiles)
endif
CFLAGS += -DNAS_ENABLE_H12_QEMU_USB_HID_USERLAND=1
endif
# H13 retains H8 original signed Alpine + NVMe as readonly backing media,
# replacing only the isolated Q35 xHCI HID provider with USB Boot Mouse.
ifeq ($(NAS_ENABLE_H13_QEMU_USB_MOUSE_USERLAND),1)
ifneq ($(NAS_ENABLE_H8_QEMU_ALPINE_ROOT)$(NAS_ENABLE_H2_PCI_RESOURCE_INVENTORY)$(NAS_ENABLE_LWEXT4),111)
$(error H13 USB Mouse requires H8 original Alpine ext4 root, H2 PCI discovery and lwext4)
endif
ifneq ($(filter 1,$(NAS_ENABLE_H9_QEMU_AHCI_USERLAND) $(NAS_ENABLE_H10_QEMU_E1000_USERLAND) $(NAS_ENABLE_H11_QEMU_XHCI_USERLAND) $(NAS_ENABLE_H12_QEMU_USB_HID_USERLAND) $(NAS_ENABLE_VIRTIO_INPUT)),)
$(error H13 isolated USB Mouse cannot share H9-H12 or VirtIO Input profiles)
endif
CFLAGS += -DNAS_ENABLE_H13_QEMU_USB_MOUSE_USERLAND=1
endif
# H14: ONE Q35 xHCI with simultaneous root-port Boot Keyboard + Mouse.
# Original signed Alpine evtest from H8 immutable NVMe ext4 only.
ifeq ($(NAS_ENABLE_H14_QEMU_DUAL_HID_USERLAND),1)
ifneq ($(NAS_ENABLE_H8_QEMU_ALPINE_ROOT)$(NAS_ENABLE_H2_PCI_RESOURCE_INVENTORY)$(NAS_ENABLE_LWEXT4),111)
$(error H14 dual HID requires H8 original signed Alpine ext4 root, H2 discovery, lwext4)
endif
ifneq ($(filter 1,$(NAS_ENABLE_H9_QEMU_AHCI_USERLAND) $(NAS_ENABLE_H10_QEMU_E1000_USERLAND) $(NAS_ENABLE_H11_QEMU_XHCI_USERLAND) $(NAS_ENABLE_H12_QEMU_USB_HID_USERLAND) $(NAS_ENABLE_H13_QEMU_USB_MOUSE_USERLAND) $(NAS_ENABLE_VIRTIO_INPUT)),)
$(error H14 dual HID cannot share H9-H13 or VirtIO Input hardware profiles)
endif
CFLAGS += -DNAS_ENABLE_H14_QEMU_DUAL_HID_USERLAND=1
endif
# H15: one Q35 xHCI FT232BM USB-serial. Original signed Alpine on
# immutable H8-derived NVMe ext4 executes BusyBox stty/head/printf over
# dynamically published /dev/ttyUSB0 (USB Bulk IN/OUT, not boot UART).
ifeq ($(NAS_ENABLE_H15_QEMU_USB_SERIAL_USERLAND),1)
ifneq ($(NAS_ENABLE_H8_QEMU_ALPINE_ROOT)$(NAS_ENABLE_H2_PCI_RESOURCE_INVENTORY)$(NAS_ENABLE_LWEXT4),111)
$(error H15 FTDI requires H8 original signed Alpine ext4 root, H2 PCI discovery and lwext4)
endif
ifneq ($(filter 1,$(NAS_ENABLE_H9_QEMU_AHCI_USERLAND) $(NAS_ENABLE_H10_QEMU_E1000_USERLAND) $(NAS_ENABLE_H11_QEMU_XHCI_USERLAND) $(NAS_ENABLE_H12_QEMU_USB_HID_USERLAND) $(NAS_ENABLE_H13_QEMU_USB_MOUSE_USERLAND) $(NAS_ENABLE_H14_QEMU_DUAL_HID_USERLAND) $(NAS_ENABLE_VIRTIO_INPUT)),)
$(error H15 isolated USB serial cannot share H9-H14 or VirtIO Input hardware profiles)
endif
CFLAGS += -DNAS_ENABLE_H15_QEMU_USB_SERIAL_USERLAND=1
endif
# Opt-in, generic Linux syscall tracing for focused real-program diagnoses.
# Normal kernel and multi-ISA builds are unaffected.
ifeq ($(NAS_SYSCALL_TRACE),1)
CFLAGS += -DNASEED_SYSCALL_TRACE
endif
ifeq ($(NAS_ENABLE_LWEXT4),1)
CFLAGS += -DNAS_ENABLE_LWEXT4=1
endif
ifeq ($(NAS_ENABLE_LITTLEFS),1)
CFLAGS += -DNAS_ENABLE_LITTLEFS=1
endif
ifeq ($(NAS_ENABLE_RAMFS),1)
CFLAGS += -DNAS_ENABLE_RAMFS=1
endif
ifeq ($(NAS_ENABLE_VIRTIO_BLK),1)
ifneq ($(NAS_ENABLE_LWEXT4),1)
$(error NAS_ENABLE_VIRTIO_BLK requires NAS_ENABLE_LWEXT4)
endif
CFLAGS += -DNAS_ENABLE_VIRTIO_BLK=1 -I ports/uboot/compat
endif
ifeq ($(NAS_ENABLE_VIRTIO_NET),1)
CFLAGS += -DNAS_ENABLE_VIRTIO_NET=1 -I ports/uboot/compat
endif
ifeq ($(NAS_ENABLE_VIRTIO_INPUT),1)
CFLAGS += -DNAS_ENABLE_VIRTIO_INPUT=1
endif
ifeq ($(NAS_ENABLE_VIRTIO_GPU),1)
# Canonical upstream v6.14 VirtIO-GPU UAPI: 2D protocol definitions are no
# longer copied by hand into the live NaSeed GPU driver.
CFLAGS += -DNAS_ENABLE_VIRTIO_GPU=1 -I ports/linux-virtgpu/include -I third_party/linux-virtgpu/include/uapi
endif
# EXPERIMENT ONLY: original TGOSKits DRM Rust archive is linked into the
# actual NaSeed x86-64 kernel and called at DRM device registration.
# Never combine this ABI probe with a second driver owner or 3D advertising.
TGOS_FFI_ARCHIVE :=
ifeq ($(NAS_EXPERIMENT_TGOS_DRM_FFI),1)
ifneq ($(NAS_ENABLE_VIRTIO_GPU),1)
$(error TGOS original-DRM FFI link test requires NAS_ENABLE_VIRTIO_GPU=1)
endif
CFLAGS += -DNAS_EXPERIMENT_TGOS_DRM_FFI=1
TGOS_FFI_ARCHIVE := build/tgos-rust-target/x86_64-unknown-none/release/libnaseed_tgoskits_drm_ffi.a
endif
# A SEPARATE real-device experiment: original TGOSKits Rust virtio-gpu owns
# the single QEMU GPU transport; the original NaSeed 2D owner is not bound.
ifeq ($(NAS_EXPERIMENT_TGOS_GPU_OWNER),1)
ifneq ($(NAS_EXPERIMENT_TGOS_DRM_FFI),1)
$(error TGOS original GPU owner requires the validated Rust DRM FFI experiment)
endif
CFLAGS += -DNAS_EXPERIMENT_TGOS_GPU_OWNER=1
endif
ifeq ($(NAS_ENABLE_VIRTIO_BLK_ASYNC),1)
CFLAGS += -DNAS_ENABLE_VIRTIO_BLK_ASYNC=1
endif
ifeq ($(NAS_ENABLE_M179_FS_IRQ),1)
ifneq ($(NAS_ENABLE_VIRTIO_BLK_ASYNC),1)
$(error M179 filesystem IRQ requires NAS_ENABLE_VIRTIO_BLK_ASYNC=1)
endif
ifneq ($(NAS_ENABLE_LWEXT4),1)
$(error M179 filesystem IRQ requires NAS_ENABLE_LWEXT4=1)
endif
CFLAGS += -DNAS_ENABLE_M179_FS_IRQ=1
endif
ifeq ($(NAS_ENABLE_M180_FS_USER_IRQ),1)
ifneq ($(NAS_ENABLE_M179_FS_IRQ),1)
$(error M180 user filesystem IRQ broker requires NAS_ENABLE_M179_FS_IRQ=1)
endif
CFLAGS += -DNAS_ENABLE_M180_FS_USER_IRQ=1
endif
ifeq ($(NAS_ENABLE_M191_ALPINE_ROOT),1)
ifneq ($(NAS_ENABLE_LWEXT4)$(NAS_ENABLE_VIRTIO_BLK),11)
$(error M191 external Alpine root requires NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1)
endif
CFLAGS += -DNAS_ENABLE_M191_ALPINE_ROOT=1
endif
ifeq ($(NAS_M241_PLATFORM_POWERDOWN),1)
CFLAGS += -DNAS_M241_PLATFORM_POWERDOWN=1
endif
ifeq ($(NAS_ENABLE_M257_DEBIAN_SHUTDOWN),1)
ifneq ($(NAS_ENABLE_M191_ALPINE_ROOT)$(NAS_ENABLE_LWEXT4)$(NAS_ENABLE_VIRTIO_BLK)$(NAS_M241_PLATFORM_POWERDOWN),1111)
$(error M257 signed Debian shutdown requires physical ext4 root and platform powerdown)
endif
CFLAGS += -DNAS_ENABLE_M257_DEBIAN_SHUTDOWN=1
endif
ifeq ($(NAS_M242_PLATFORM_REBOOT),1)
CFLAGS += -DNAS_M242_PLATFORM_REBOOT=1
endif
ifeq ($(NAS_ENABLE_M253_NONROOT_DAC),1)
ifneq ($(NAS_M253_DAC_ENFORCEMENT_COMPLETE),1)
$(error M253 nonroot credentials require explicit complete VFS DAC enforcement)
endif
CFLAGS += -DNAS_ENABLE_M253_NONROOT_DAC=1 -DNAS_M253_DAC_ENFORCEMENT_COMPLETE=1
endif
CFLAGS += -DNAS_ENABLE_LWIP=1 -I ports/lwip/include -I third_party/lwip/src/include
# Baseline C kernel retains its original script. This opt-in only uses a
# variant that includes Rust GOTPCREL entries in a real PT_LOAD segment.
# Nothing changes on the normal VirtIO GPU/2D builds.
NAS_KERNEL_LINKER := kernel/arch/$(ARCH)/linker.ld
ifeq ($(NAS_EXPERIMENT_TGOS_DRM_FFI),1)
ifneq ($(ARCH),x86_64)
$(error TGOS original Rust DRM FFI experiment is x86_64-only until independently validated)
endif
NAS_KERNEL_LINKER := ports/tgoskits-drm-ffi/kernel-x86_64-ffi.ld
endif
LDFLAGS := -m elf_x86_64 -nostdlib -static -z max-page-size=0x1000 \
 -z noexecstack -T $(NAS_KERNEL_LINKER)
# The upstream DRM archive contains many ORIGINAL but currently unreachable
# Linux DRM helpers. They refer to OTHER DRM modules that NaSeed has not
# enabled. Collect only reachable function sections in this optional ABI
# experiment; any unresolved symbol on the executed ABI path still fails.
# The normal kernel remains unchanged and never uses this linker option.
ifeq ($(NAS_EXPERIMENT_TGOS_DRM_FFI),1)
LDFLAGS += --gc-sections
endif

H0_HW_OBJS :=
ifeq ($(NAS_ENABLE_H0_HARDWARE_DISCOVERY),1)
UACPI_FLAGS := -DUACPI_BAREBONES_MODE -I third_party/uacpi/include
UACPI_SRCS := $(addprefix third_party/uacpi/source/,tables.c uacpi.c stdlib.c utilities.c)
H0_HW_SRCS := $(UACPI_SRCS) kernel/platform/firmware_acpi.c \
 kernel/drivers/core/pci_scan.c kernel/platform/x86_h0_pci.c
ifeq ($(NAS_ENABLE_H1_ECAM_DISCOVERY),1)
H0_HW_SRCS += kernel/platform/pci_ecam.c kernel/platform/x86_h1_ecam.c
endif
ifeq ($(NAS_ENABLE_H2_PCI_RESOURCE_INVENTORY),1)
H0_HW_SRCS += kernel/drivers/core/pci_resources.c
endif
ifeq ($(NAS_ENABLE_H3_QEMU_NVME_IDENTIFY),1)
H0_HW_SRCS += kernel/platform/x86_h3_nvme.c \
 kernel/drivers/nvme/identify.c kernel/drivers/port/dma.c
endif
ifeq ($(NAS_ENABLE_H4_QEMU_NVME_READONLY),1)
H0_HW_SRCS += kernel/drivers/nvme/readonly.c kernel/drivers/port/block.c
endif
ifeq ($(NAS_ENABLE_H9_QEMU_AHCI_USERLAND),1)
H0_HW_SRCS += kernel/platform/x86_h9_ahci.c \
 kernel/drivers/ahci/readonly.c \
 kernel/drivers/port/dma.c kernel/drivers/port/block.c
endif
ifeq ($(NAS_ENABLE_H10_QEMU_E1000_USERLAND),1)
H0_HW_SRCS += kernel/platform/x86_h10_e1000.c \
 kernel/drivers/e1000/e1000_nic.c
endif
ifeq ($(NAS_ENABLE_H11_QEMU_XHCI_USERLAND),1)
H0_HW_SRCS += kernel/platform/x86_h11_usb.c \
 kernel/drivers/usb/xhci.c kernel/drivers/usb/msc.c \
 kernel/drivers/port/dma.c kernel/drivers/port/block.c
endif
ifneq ($(filter 1,$(NAS_ENABLE_H12_QEMU_USB_HID_USERLAND) $(NAS_ENABLE_H13_QEMU_USB_MOUSE_USERLAND) $(NAS_ENABLE_H14_QEMU_DUAL_HID_USERLAND)),)
H0_HW_SRCS += kernel/drivers/usb/xhci.c kernel/drivers/usb/hid_boot.c \
 kernel/drivers/usb/hid_boot_mouse.c
endif
ifeq ($(NAS_ENABLE_H12_QEMU_USB_HID_USERLAND),1)
H0_HW_SRCS += kernel/platform/x86_h12_hid.c
endif
ifeq ($(NAS_ENABLE_H13_QEMU_USB_MOUSE_USERLAND),1)
H0_HW_SRCS += kernel/platform/x86_h13_mouse.c
endif
ifeq ($(NAS_ENABLE_H14_QEMU_DUAL_HID_USERLAND),1)
H0_HW_SRCS += kernel/platform/x86_h14_dual_hid.c
endif
ifeq ($(NAS_ENABLE_H15_QEMU_USB_SERIAL_USERLAND),1)
H0_HW_SRCS += kernel/platform/x86_h15_ftdi.c \
  kernel/drivers/usb/xhci.c kernel/fs/usb_serial_chrdev.c
endif
H0_HW_OBJS := $(patsubst %.c,$(OUT)/h0_%.o,$(notdir $(H0_HW_SRCS)))
$(OUT)/h0_firmware_acpi.o: private CFLAGS += $(UACPI_FLAGS)
$(patsubst %.c,$(OUT)/h0_%.o,$(notdir $(UACPI_SRCS))): private CFLAGS += $(UACPI_FLAGS) -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable

define nas_compile_h0_hw
$(OUT)/h0_$(notdir $(basename $(1))).o: $(1) $(HDR)
	mkdir -p $(OUT)
	clang $$(CFLAGS) -MMD -MP -c $$< -o $$@
endef
$(foreach source,$(H0_HW_SRCS),$(eval $(call nas_compile_h0_hw,$(source))))
endif

# Opt-in unchanged original U-Boot source behind a modern PCI transport.
# All request code is shared with ARM/RV; only x86 PCI enumeration is extra.
VIRTIO_X86_OBJS :=
ifneq ($(filter 1,$(NAS_ENABLE_VIRTIO_BLK) $(NAS_ENABLE_VIRTIO_NET) $(NAS_ENABLE_VIRTIO_INPUT) $(NAS_ENABLE_VIRTIO_GPU) $(NAS_ENABLE_VIRTIO_BLK_ASYNC)),)
VIRTIO_X86_SRCS := kernel/drivers/port/dma.c \
 kernel/drivers/virtio/transport.c \
 kernel/drivers/virtio/pci.c ports/unikraft/split_queue.c \
 kernel/platform/x86_virtio_pci.c
ifneq ($(filter 1,$(NAS_ENABLE_VIRTIO_BLK) $(NAS_ENABLE_VIRTIO_NET)),)
VIRTIO_X86_SRCS += ports/uboot/virtio_compat.c
endif
ifeq ($(NAS_ENABLE_VIRTIO_BLK),1)
VIRTIO_X86_SRCS += kernel/drivers/port/block.c ports/uboot/blk_port.c ports/uboot/virtio_bus.c
endif
ifeq ($(NAS_ENABLE_VIRTIO_NET),1)
VIRTIO_X86_SRCS += ports/uboot/net_port.c ports/uboot/virtio_net_bus.c
endif
ifeq ($(NAS_ENABLE_VIRTIO_INPUT),1)
VIRTIO_X86_SRCS += kernel/drivers/input/virtio_input.c
endif
ifeq ($(NAS_ENABLE_VIRTIO_GPU),1)
VIRTIO_X86_SRCS += kernel/drivers/display/virtio_gpu.c kernel/fs/drm_chrdev.c kernel/fs/drm_gem.c kernel/fs/drm_kms.c ports/linux-virtgpu/virtqueue_bridge.c ports/linux-virtgpu/virtqueue_api_port.c ports/linux-virtgpu/vq_pair_port.c ports/linux-virtgpu/gem_dma_port.c ports/linux-virtgpu/vbuf_dma_port.c
ifeq ($(NAS_EXPERIMENT_TGOS_DRM_FFI),1)
VIRTIO_X86_SRCS += ports/tgoskits-drm-ffi/na_kernel_bridge.c
ifeq ($(NAS_EXPERIMENT_TGOS_GPU_OWNER),1)
VIRTIO_X86_SRCS += ports/tgoskits-drm-ffi/gpu_kernel_port.c
endif
endif
endif
ifeq ($(NAS_ENABLE_VIRTIO_BLK_ASYNC),1)
VIRTIO_X86_SRCS += kernel/block/request.c ports/uboot/async_blk.c
endif
VIRTIO_X86_OBJS := $(patsubst %.c,$(OUT)/virtio_%.o,$(notdir $(VIRTIO_X86_SRCS)))
define nas_compile_x86_virtio
$(OUT)/virtio_$(notdir $(basename $(1))).o: $(1) $(HDR)
	mkdir -p $(OUT)
	clang $(CFLAGS) -MMD -MP -c $$< -o $$@
endef
$(foreach source,$(VIRTIO_X86_SRCS),$(eval $(call nas_compile_x86_virtio,$(source))))
endif

# Shared Linux input-core/evdev plus provider-based character endpoints.
# No input stack is linked into the old serial-only hardware profile.
NAS_INPUT_COMMON_OBJS :=
ifneq ($(filter 1,$(NAS_ENABLE_VIRTIO_INPUT) $(NAS_ENABLE_H12_QEMU_USB_HID_USERLAND) $(NAS_ENABLE_H13_QEMU_USB_MOUSE_USERLAND) $(NAS_ENABLE_H14_QEMU_DUAL_HID_USERLAND)),)
NAS_INPUT_COMMON_SRCS := kernel/drivers/input/core.c \
 kernel/drivers/input/evdev.c kernel/fs/input_chrdev.c
NAS_INPUT_COMMON_OBJS += $(patsubst %.c,$(OUT)/nasinput_%.o,$(notdir $(NAS_INPUT_COMMON_SRCS)))
define nas_compile_x86_input
$(OUT)/nasinput_$(notdir $(basename $(1))).o: $(1) $(HDR)
	mkdir -p $(OUT)
	clang $(CFLAGS) -MMD -MP -c $$< -o $$@
endef
$(foreach source,$(NAS_INPUT_COMMON_SRCS),$(eval $(call nas_compile_x86_input,$(source))))
endif

# Official pinned lwIP source list: the only AF_INET provider on every ISA.
# The U-Boot NIC driver remains optional; upstream lwIP loopback is not.
LWIP_X86_SRCS := ports/lwip/libc_port.c ports/lwip/sys_arch.c ports/lwip/netif_port.c \
 ports/lwip/network_boot.c ports/lwip/netconn_bridge.c \
 ports/lwip/socket_port.c \
 $(wildcard third_party/lwip/src/core/*.c) \
 $(wildcard third_party/lwip/src/core/ipv4/*.c) \
 $(wildcard third_party/lwip/src/api/*.c) \
 third_party/lwip/src/netif/ethernet.c
LWIP_X86_OBJS := $(patsubst %.c,$(OUT)/lwip_%.o,$(notdir $(LWIP_X86_SRCS)))
define nas_compile_x86_lwip
$(OUT)/lwip_$(notdir $(basename $(1))).o: $(1) $(HDR)
	mkdir -p $(OUT)
	clang $(CFLAGS) -I ports/lwip/freestanding -MMD -MP -c $$< -o $$@
endef
$(foreach source,$(LWIP_X86_SRCS),$(eval $(call nas_compile_x86_lwip,$(source))))

# Opt-in until the genuine guest VFS mount and syscall workflow is complete.
LWEXT4_EXTRA :=
ifeq ($(NAS_ENABLE_LWEXT4),1)
LWEXT4_EXTRA := $(OUT)/lwext4_guest.o
endif
LITTLEFS_EXTRA :=
ifeq ($(NAS_ENABLE_LITTLEFS),1)
LITTLEFS_EXTRA := $(OUT)/littlefs_guest.o
endif
$(OUT)/littlefs_guest.o: scripts/build-littlefs-guest.sh $(HDR)
	sh scripts/build-littlefs-guest.sh x86_64 $(OUT)
RAMFS_EXTRA :=
ifeq ($(NAS_ENABLE_RAMFS),1)
RAMFS_EXTRA := $(OUT)/ramfs_guest.o
endif
$(OUT)/ramfs_guest.o: scripts/build-ramfs-guest.sh $(HDR)
	sh scripts/build-ramfs-guest.sh x86_64 $(OUT)
$(OUT)/lwext4_guest.o: scripts/build-lwext4-guest.sh ports/lwext4/freestanding/platform.h $(HDR)
	sh scripts/build-lwext4-guest.sh x86_64 $(OUT) kernel


$(HDR):
	mkdir -p $(dir $@)
	curl -fL --retry 3 -o $@.tmp \
	 https://raw.githubusercontent.com/Limine-Bootloader/limine-protocol/$(PROTO_REV)/include/limine.h
	mv $@.tmp $@


$(OUT)/kernel.o: kernel/kernel.c kernel/arch/arch.h kernel/drivers/serial.h kernel/mm/frame.h $(HDR)
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/random.o: kernel/random.c kernel/random.h kernel/mm/usercopy.h third_party/chacha20/crypto/chacha20/chacha.h third_party/chacha20/crypto/chacha20/_chacha.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -I ports/chacha20/freestanding -I third_party/chacha20 -MMD -MP -c $< -o $@

$(OUT)/chacha20.o: third_party/chacha20/crypto/chacha20/chacha.c third_party/chacha20/crypto/chacha20/chacha.h third_party/chacha20/crypto/chacha20/_chacha.h $(wildcard ports/chacha20/freestanding/sys/*.h)
	mkdir -p $(dir $@)
	clang $(CFLAGS) -I ports/chacha20/freestanding -I third_party/chacha20 -c $< -o $@

$(OUT)/timekeeper.o: kernel/time/clock.c kernel/time/clock.h kernel/arch/arch.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/uts.o: kernel/uts.c kernel/uts.h kernel/lib/string.h
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

$(OUT)/task_accounting.o: kernel/task/accounting.c kernel/task.h kernel/arch/arch.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/task_resources.o: kernel/task/resources.c kernel/task/resources.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/exception.o: kernel/exception/exception.c kernel/exception/exception.h kernel/mm/fault.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/mm_fault.o: kernel/mm/fault.c kernel/mm/fault.h kernel/mm/mm.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/nas_mm.o: kernel/mm/mm.c kernel/mm/mm.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/task_kernel_context.o: kernel/task/kernel_context.c kernel/task/kernel_context.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/task_wait_queue.o: kernel/task/wait_queue.c kernel/task/wait_queue.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/task_futex_wait.o: kernel/task/futex_wait.c kernel/task/futex_wait.h kernel/task/wait_queue.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/task_run_state.o: kernel/task/run_state.c kernel/task/run_state.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/task_blocked_completion.o: kernel/task/blocked_completion.c kernel/task/blocked_completion.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/task_kthread.o: kernel/task/kthread.c kernel/task/kthread.h kernel/task/kernel_context.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/task_kthread_boot.o: kernel/task/kthread_boot.c kernel/task/kthread.h kernel/task/wait_queue.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/task_k_sync.o: kernel/task/k_sync.c kernel/task/k_sync.h kernel/task/kthread.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/driver_bus.o: kernel/drivers/core/bus.c kernel/drivers/core/bus.h kernel/errno.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/driver_irq.o: kernel/drivers/core/irq.c kernel/drivers/core/irq.h kernel/errno.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

# The device-node registry and built-in class devices exist in every profile.
$(OUT)/driver_chrdev.o: kernel/drivers/core/chrdev.c kernel/drivers/core/chrdev.h kernel/errno.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/driver_misc.o: kernel/drivers/core/misc.c kernel/drivers/core/misc.h kernel/drivers/core/chrdev.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fbdev_chrdev.o: kernel/drivers/display/fbdev_chrdev.c kernel/drivers/display/fbdev.h kernel/drivers/core/chrdev.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/task_scheduler_service.o: kernel/task/scheduler_service.c kernel/task/scheduler_service.h kernel/task/kthread.h kernel/drivers/core/bus.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/task_sched_core.o: kernel/task/sched_core.c kernel/task/sched_core.h kernel/task/wait.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/arch_kernel_switch.o: kernel/arch/$(ARCH)/kernel_switch.S
	mkdir -p $(dir $@)
	clang --target=x86_64-unknown-none-elf -mno-red-zone -c $< -o $@

$(OUT)/task.o: kernel/task.c kernel/task.h kernel/fd.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/remote_copy.o: kernel/mm/remote_copy.c kernel/mm/vm.h kernel/arch/mmu.h kernel/task.h kernel/mm/frame.h
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

$(OUT)/fs_eventfd.o: kernel/fs/eventfd.c kernel/fs/eventfd.h kernel/fd.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_timerfd.o: kernel/fs/timerfd.c kernel/fs/timerfd.h kernel/time/clock.h kernel/fd.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_inotify.o: kernel/fs/inotify.c kernel/fs/inotify.h kernel/fs/vfs.h kernel/fd.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_unix_socket.o: kernel/fs/unix_socket.c kernel/fs/unix_socket.h kernel/pipe.h kernel/fd.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_epoll.o: kernel/fs/epoll.c kernel/fs/epoll.h kernel/fd.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_procfs.o: kernel/fs/procfs.c kernel/fs/procfs.h kernel/task.h kernel/mm/frame.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/ipc_sysv.o: kernel/ipc/sysv.c kernel/ipc/sysv.h kernel/task.h kernel/mm/usercopy.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/net_device_port.o: kernel/drivers/port/net.c kernel/drivers/port/net.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_pidfd.o: kernel/fs/pidfd.c kernel/fs/pidfd.h kernel/fd.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/block_core.o: kernel/block/device.c kernel/block/device.h kernel/errno.h kernel/lib/string.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/block_ramdisk.o: kernel/block/ramdisk.c kernel/block/ramdisk.h kernel/block/device.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/block_partition.o: kernel/block/partition.c kernel/block/partition.h kernel/block/device.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/mm_heap.o: kernel/mm/heap.c kernel/mm/heap.h third_party/tlsf/tlsf.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/tlsf.o: third_party/tlsf/tlsf.c third_party/tlsf/tlsf.h $(wildcard ports/tlsf/freestanding/*.h)
	mkdir -p $(dir $@)
	clang $(CFLAGS) -I ports/tlsf/freestanding -MMD -MP -c $< -o $@

$(OUT)/fs_vfs.o: kernel/fs/vfs.c kernel/fs/vfs.h kernel/fs/lookup.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_vfs_providers.o: kernel/fs/vfs_providers.c kernel/fs/vfs.h kernel/fs/lookup.h kernel/bootfs.h kernel/tmpfs.h kernel/fs/procfs.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_devfs.o: kernel/fs/devfs.c kernel/fs/vfs.h kernel/drivers/core/chrdev.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_lookup.o: kernel/fs/lookup.c kernel/fs/lookup.h kernel/tmpfs.h kernel/bootfs.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_namespace_syscall.o: kernel/fs/namespace_syscall.c kernel/fs/namespace_syscall.h kernel/fs/syscall.h kernel/tmpfs.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_socket_syscall.o: kernel/fs/socket_syscall.c kernel/fs/socket_syscall.h kernel/fs/unix_socket.h kernel/fs/netlink_core.h kernel/fd.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_netlink_core.o: kernel/fs/netlink_core.c kernel/fs/netlink_core.h kernel/fs/netlink_route.h kernel/fs/netlink_filter.h kernel/fs/netlink_generic.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_netlink_route.o: kernel/fs/netlink_route.c kernel/fs/netlink_route.h kernel/fs/file_ops.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_netlink_filter.o: kernel/fs/netlink_filter.c kernel/fs/netlink_filter.h kernel/task/capability.h kernel/fs/file_ops.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_netlink_generic.o: kernel/fs/netlink_generic.c kernel/fs/netlink_generic.h kernel/fs/file_ops.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_netlink_kobject.o: kernel/fs/netlink_kobject.c kernel/fs/netlink_kobject.h kernel/fs/netlink_common.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_packet_socket.o: kernel/fs/packet_socket.c kernel/fs/packet_socket.h kernel/fs/file_ops.h kernel/drivers/port/net.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_memfd.o: kernel/fs/memfd.c kernel/fs/memfd.h kernel/fs/open_path.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_file_io.o: kernel/fs/file_io.c kernel/fs/io.h kernel/fs/file_ops.h kernel/fd.h kernel/tmpfs.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_broker.o: kernel/fs/broker.c kernel/fs/broker.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_user_broker.o: kernel/fs/user_broker.c kernel/fs/user_broker.h kernel/fs/broker.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_poll_syscall.o: kernel/fs/poll_syscall.c kernel/fs/io.h kernel/fs/file_ops.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_metadata_syscall.o: kernel/fs/metadata_syscall.c kernel/fs/metadata_syscall.h kernel/fs/open_path.h kernel/tmpfs.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_syscall.o: kernel/fs/syscall.c kernel/fs/syscall.h kernel/fs/io.h kernel/fd.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fbdev.o: kernel/drivers/display/fbdev.c kernel/drivers/display/fbdev.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fbcon.o: kernel/drivers/display/fbcon.c kernel/drivers/display/fbcon.h kernel/drivers/display/display.h third_party/flanterm/src/flanterm.h third_party/flanterm/src/flanterm_backends/fb.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/flanterm.o: third_party/flanterm/src/flanterm.c third_party/flanterm/src/flanterm.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/flanterm_fb.o: third_party/flanterm/src/flanterm_backends/fb.c third_party/flanterm/src/flanterm_backends/fb.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -DFLANTERM_FB_DISABLE_BUMP_ALLOC -MMD -MP -c $< -o $@

$(OUT)/session.o: kernel/task/session.c kernel/task/session.h kernel/task.h kernel/errno.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/process.o: kernel/task/process.c kernel/task/process.h kernel/arch/arch.h kernel/arch/mmu.h kernel/mm/usercopy.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/ptrace.o: kernel/task/ptrace.c kernel/task/ptrace.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/task_wait.o: kernel/task/wait.c kernel/task/wait.h kernel/task.h kernel/task/process.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/sched.o: kernel/task/sched.c kernel/task/sched.h kernel/task/process.h kernel/fs/io.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/bootfs.o: kernel/bootfs.c kernel/bootfs.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fs_tmp_namespace.o: kernel/fs/tmp_namespace.c kernel/tmpfs.h kernel/bootfs.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/tmpfs.o: kernel/tmpfs.c kernel/tmpfs.h kernel/fd.h kernel/task.h kernel/bootfs.h kernel/errno.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/fd.o: kernel/fd.c kernel/fd.h kernel/task.h kernel/errno.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/pipe.o: kernel/pipe.c kernel/pipe.h kernel/fd.h kernel/task.h kernel/errno.h kernel/fs/epoll.h
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

$(OUT)/vt.o: kernel/drivers/vt.c kernel/drivers/vt.h kernel/drivers/tty.h kernel/task.h
	mkdir -p $(dir $@)
	clang $(CFLAGS) -MMD -MP -c $< -o $@


$(OUT)/pty.o: kernel/drivers/pty.c kernel/drivers/pty.h kernel/drivers/tty.h kernel/drivers/tty_ldisc.h kernel/fd.h
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

$(KERNEL): $(OUT)/driver_bus.o $(OUT)/driver_irq.o $(OUT)/driver_chrdev.o $(OUT)/driver_misc.o $(OUT)/fbdev_chrdev.o $(OUT)/fbdev.o $(OUT)/fbcon.o $(OUT)/flanterm.o $(OUT)/flanterm_fb.o $(OUT)/kernel.o $(OUT)/random.o $(OUT)/chacha20.o $(OUT)/timekeeper.o $(OUT)/uts.o $(OUT)/init_main.o $(OUT)/task_exec.o $(OUT)/syscall_dispatch.o $(OUT)/task_accounting.o $(OUT)/task.o $(OUT)/task_kernel_context.o $(OUT)/task_wait_queue.o $(OUT)/task_futex_wait.o $(OUT)/task_run_state.o $(OUT)/task_blocked_completion.o $(OUT)/task_kthread.o $(OUT)/task_kthread_boot.o $(OUT)/task_k_sync.o $(OUT)/task_scheduler_service.o $(OUT)/task_sched_core.o $(OUT)/nas_mm.o $(OUT)/exception.o $(OUT)/mm_fault.o $(OUT)/task_resources.o $(OUT)/process.o $(OUT)/ptrace.o $(OUT)/session.o $(OUT)/sched.o $(OUT)/task_wait.o $(OUT)/bootfs.o $(OUT)/tmpfs.o $(OUT)/block_core.o $(OUT)/block_ramdisk.o $(OUT)/block_partition.o $(OUT)/mm_heap.o $(OUT)/tlsf.o $(OUT)/fs_tmp_namespace.o $(OUT)/fd.o $(OUT)/pipe.o $(OUT)/boot_image.o $(OUT)/arch.o $(OUT)/arch_cpu.o $(OUT)/arch_context.o $(OUT)/arch_kernel_switch.o $(OUT)/arch_timer.o $(OUT)/serial.o $(OUT)/tty.o $(OUT)/vt.o $(OUT)/pty.o $(OUT)/tty_ldisc.o $(OUT)/arch_console.o $(OUT)/frame.o $(OUT)/platform.o $(OUT)/arch_mmu.o $(OUT)/vm.o $(OUT)/anon_regions.o $(OUT)/remote_copy.o $(OUT)/usercopy.o $(OUT)/elf.o $(OUT)/elf_segments.o $(OUT)/user_stack.o $(OUT)/signal.o $(OUT)/arch_signal_frame.o $(OUT)/klib_string.o $(OUT)/arch_syscall.o $(OUT)/fs_procfs.o $(OUT)/ipc_sysv.o $(OUT)/net_device_port.o $(OUT)/fs_pidfd.o $(OUT)/fs_lookup.o $(OUT)/fs_vfs.o $(OUT)/fs_vfs_providers.o $(OUT)/fs_devfs.o $(OUT)/fs_epoll.o $(OUT)/fs_eventfd.o $(OUT)/fs_timerfd.o $(OUT)/fs_inotify.o $(OUT)/fs_unix_socket.o $(OUT)/fs_namespace_syscall.o $(OUT)/fs_socket_syscall.o $(OUT)/fs_netlink_core.o $(OUT)/fs_netlink_route.o $(OUT)/fs_netlink_filter.o $(OUT)/fs_netlink_generic.o $(OUT)/fs_netlink_kobject.o $(OUT)/fs_packet_socket.o $(OUT)/fs_memfd.o $(OUT)/fs_file_io.o $(OUT)/fs_broker.o $(OUT)/fs_user_broker.o $(OUT)/fs_poll_syscall.o $(OUT)/fs_metadata_syscall.o $(OUT)/fs_syscall.o $(LWEXT4_EXTRA) $(LITTLEFS_EXTRA) $(RAMFS_EXTRA) $(VIRTIO_X86_OBJS) $(NAS_INPUT_COMMON_OBJS) $(LWIP_X86_OBJS) $(NAS_KERNEL_LINKER)
	ld.lld $(LDFLAGS) $(filter %.o,$^) $(TGOS_FFI_ARCHIVE) -o $@

# A native Rust library is mandatory for this experiment; build errors
# are not skipped and a missing upstream symbol fails the *real* kernel link.
ifeq ($(NAS_EXPERIMENT_TGOS_DRM_FFI),1)
$(TGOS_FFI_ARCHIVE): scripts/build-tgoskits-drm-ffi.sh ports/tgoskits-drm-ffi/src/lib.rs ports/tgoskits-drm-ffi/Cargo.toml ports/tgoskits-drm-ffi/src/gpu.rs
	sh scripts/build-tgoskits-drm-ffi.sh
$(KERNEL): $(TGOS_FFI_ARCHIVE)
endif

$(KERNEL): $(H0_HW_OBJS)

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

.PHONY: all iso kernel native smoke run clean check-host
ifeq ($(ARCH),x86_64)
all: iso
iso: $(ISO)
kernel: $(KERNEL)
else
all: kernel
kernel: native
iso:
	@echo "ISO currently requires ARCH=x86_64" >&2; exit 2
endif
native:
	@case "$(ARCH)" in riscv64|aarch64) ;; *) echo "native requires ARCH=riscv64 or ARCH=aarch64" >&2; exit 2;; esac
	sh scripts/build-native-full.sh "$(ARCH)"
smoke: iso
	sh tests/boot-smoke.sh $(ISO)
# Host-only contract checks compile production objects with strict diagnostics.
check-host:
	mkdir -p $(OUT)/test
	cc -std=c11 -O2 -Wall -Wextra -Werror kernel/drivers/display/fbdev.c tests/display-contract.c -o $(OUT)/test/display-contract
	$(OUT)/test/display-contract
	cc -std=c11 -O2 -Wall -Wextra -Werror kernel/mm/anon_regions.c tests/vma-contract.c -o $(OUT)/test/vma-contract
	$(OUT)/test/vma-contract
	cc -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections kernel/mm/vm.c tests/vm-truncate-contract.c -Wl,--gc-sections -o $(OUT)/test/vm-truncate-contract
	$(OUT)/test/vm-truncate-contract
	cc -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections kernel/tmpfs.c tests/tmpfs-size-contract.c -Wl,--gc-sections -o $(OUT)/test/tmpfs-size-contract
	$(OUT)/test/tmpfs-size-contract
	cc -std=c11 -O2 -Wall -Wextra -Werror -I ports/tlsf/freestanding kernel/mm/heap.c third_party/tlsf/tlsf.c tests/heap-contract.c -o $(OUT)/test/heap-contract
	$(OUT)/test/heap-contract
	clang -std=gnu11 -O2 -Wall -Wextra -DUACPI_BAREBONES_MODE -I third_party/uacpi/include kernel/platform/firmware_acpi.c third_party/uacpi/source/tables.c third_party/uacpi/source/uacpi.c third_party/uacpi/source/stdlib.c third_party/uacpi/source/utilities.c tests/acpi-contract.c -o $(OUT)/test/acpi-contract
	$(OUT)/test/acpi-contract
	clang -std=c11 -O2 -Wall -Wextra -DFLANTERM_FB_DISABLE_BUMP_ALLOC kernel/drivers/display/fbdev.c kernel/drivers/display/fbcon.c third_party/flanterm/src/flanterm.c third_party/flanterm/src/flanterm_backends/fb.c tests/fbcon-contract.c -o $(OUT)/test/fbcon-contract
	$(OUT)/test/fbcon-contract
	clang -std=gnu11 -O2 -Wall -Wextra -I third_party/libfdt kernel/platform/firmware_fdt.c third_party/libfdt/fdt.c third_party/libfdt/fdt_ro.c third_party/libfdt/fdt_check.c third_party/libfdt/fdt_sw.c tests/fdt-contract.c -o $(OUT)/test/fdt-contract
	$(OUT)/test/fdt-contract
	clang -std=c11 -O2 -Wall -Wextra -Werror -I ports/chacha20/freestanding -I third_party/chacha20 kernel/random.c third_party/chacha20/crypto/chacha20/chacha.c tests/random-contract.c -o $(OUT)/test/random-contract
	$(OUT)/test/random-contract
run: iso
	sh scripts/run.sh $(ISO)
clean:
	rm -rf $(OUT)
-include $(wildcard $(OUT)/*.d)
