/* Ordinary libc-using C program; no M35 special headers/runtime. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    char *p = malloc(128);
    if (!p) return 7;
    strcpy(p, "M38_GCC_PASS");
    puts(p);
    free(p);
    return 0;
}
