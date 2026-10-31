/* Architecture-neutral ELF64 assembly data emitted in a read-only section.
 * The dynamic relocation is intentionally in .text for eu-findtextrel.
 * This test artifact is linked ON THE LINUX HOST and never executed. */
.text
.globl m118_textrel_anchor
m118_textrel_anchor:
    .quad m118_textrel_value
.data
.globl m118_textrel_value
m118_textrel_value:
    .quad 0
