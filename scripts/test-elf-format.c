#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "../kernel/task/elf_format.h"
/* Host regression: the SAME ELF validator used for native and RV64 loading. */
int main(void) {
    union { uint64_t align; uint8_t bytes[4096]; } image = {0};
    struct elf_hdr *h = (struct elf_hdr *)(void *)image.bytes;
    struct elf_phdr *p = (struct elf_phdr *)(void *)(image.bytes + sizeof(*h));
    memcpy(h->ident, "\177ELF", 4);
    h->ident[4] = 2; h->ident[5] = 1; h->ident[6] = 1;
    h->type = 2; h->version = 1; h->phoff = sizeof(*h);
    h->phnum = 1; h->phentsize = sizeof(*p);
    p->type = 1; p->offset = 512; p->filesz = 12; p->memsz = 32;
    struct nas_elf_view v;
    const uint16_t machines[] = {62, 243, 183};
    for (unsigned i = 0; i < 3; i++) {
        h->machine = machines[i];
        assert(nas_elf_open(&v, image.bytes, sizeof(image.bytes),
                            machines[i], false, 16));
        assert(v.header == h && v.phdrs == p);
        assert(nas_elf_segment_file_ok(p, sizeof(image.bytes)));
        assert(!nas_elf_open(&v, image.bytes, sizeof(image.bytes),
                             machines[(i + 1) % 3], false, 16));
    }
    h->machine = 243;
    h->type = 3;
    assert(!nas_elf_open(&v, image.bytes, sizeof(image.bytes), 243, false, 16));
    assert(nas_elf_open(&v, image.bytes, sizeof(image.bytes), 243, true, 16));
    h->type = 2;
    h->phoff = UINT64_MAX - 1;
    assert(!nas_elf_open(&v, image.bytes, sizeof(image.bytes), 243, false, 16));
    h->phoff = sizeof(*h);
    p->filesz = 33;
    assert(!nas_elf_segment_file_ok(p, sizeof(image.bytes)));
    p->filesz = 12;
    p->offset = UINT64_MAX - 5;
    assert(!nas_elf_segment_file_ok(p, sizeof(image.bytes)));
    return 0;
}
