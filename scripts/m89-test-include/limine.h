#ifndef NASEED_M89_TEST_LIMINE_TYPES_H
#define NASEED_M89_TEST_LIMINE_TYPES_H
/* Host-only type shim for compiling the actual file_io.c translation unit.
 * The poll_description path never inspects bootloader data; the test uses
 * section GC to discard unrelated ELF/archive readers. Native kernel builds
 * continue to use the genuine pinned Limine header. */
#include <stdint.h>
struct limine_file {void *address; uint64_t size;};
#endif
