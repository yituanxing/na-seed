OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x400000;
    .text : { *(.text .text.*) *(.rodata .rodata.*) } :text
    . = ALIGN(0x1000);
    .data : { *(.data .data.*) } :data
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) }
}
