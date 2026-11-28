/* NaSeed x86-64 experimental Rust DRM linker layout.
 * Based on kernel/arch/x86_64/linker.ld. Keep original NaSeed boot layout.
 * Unlike the C-only baseline, the pinned Rust kdrm-core static archive uses
 * GOTPCREL calls (e.g. Rust write_bytes -> C memset). DROPPING .got lets
 * lld resolve that indirect call through address ZERO and triple-fault QEMU.
 * Never advertise a linked Rust kernel without preserving this table. */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kmain)
PHDRS {
  requests PT_LOAD FLAGS(0x06);
  text PT_LOAD FLAGS(0x05);
  rodata PT_LOAD FLAGS(0x04);
  data PT_LOAD FLAGS(0x06);
}
SECTIONS {
  . = 0xffffffff80000000;
  .limine_requests : {
    KEEP(*(.limine_requests_start))
    KEEP(*(.limine_requests))
    KEEP(*(.limine_requests_end))
  } :requests
  . = ALIGN(4096);
  .text : { *(.text .text.*) } :text
  . = ALIGN(4096);
  .rodata : { *(.rodata .rodata.*) } :rodata
  . = ALIGN(4096);
  .data : { *(.data .data.*) } :data
  . = ALIGN(4096);
  .got : { *(.got .got.*) } :data
  .bss : { *(.bss .bss.*) *(COMMON) } :data
  /DISCARD/ : { *(.eh_frame*) *(.note .note.*) }
}
