OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); }
SECTIONS {
  . = 0x400000;
  .text : { *(.text .text.*) *(.rodata .rodata.*) } :text
  /DISCARD/ : { *(.note .note.*) *(.eh_frame*) }
}
