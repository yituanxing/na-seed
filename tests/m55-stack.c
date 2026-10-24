#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Real guest-compiled, ISA-neutral C stack regression: the old 128-KiB
 * main-thread mapping faults on the first touch below 0x6ffe0000.
 * Volatile page writes forbid the compiler from optimizing it away. */
__attribute__((noinline)) static unsigned deep_stack(void) {
    volatile uint8_t pages[256U*1024U];
    unsigned sum=0;
    for(size_t off=0;off<sizeof(pages);off+=4096U)
        pages[off]=(uint8_t)(off/4096U);
    for(size_t off=0;off<sizeof(pages);off+=4096U)
        sum+=(unsigned)pages[off];
    return sum;
}
int main(void) {
    assert(deep_stack()==2016U);
    puts("M55_NATIVE_MAIN_STACK_256K_PASS");
    return 0;
}
