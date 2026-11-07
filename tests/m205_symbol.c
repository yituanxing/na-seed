/* Host-built target-ISA DWARF fixture. Never accepted as an Alpine program.
 * The guest executes the genuine signed llvm21-symbolizer against this ELF.
 * Keep both the function and the source-line mapping observable at -O0. */
__attribute__((noinline)) int m205_symbol(void) {
    volatile int x = 41;
    return x + 1;
}

int main(void) {
    return m205_symbol() == 42 ? 0 : 1;
}
