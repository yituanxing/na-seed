#ifndef NASEED_FRAME_H
#define NASEED_FRAME_H
#include <stdint.h>
#include <stdbool.h>
struct limine_memmap_response;
bool nas_frames_init(const struct limine_memmap_response *, uint64_t);
uint64_t nas_free_frames(void);
uint64_t nas_managed_frames(void);
uint64_t nas_frame_alloc(void);
/* Allocate grants one owner; get increments; put drops only one reference.
 * The final put returns the 4 KiB frame to the free bitmap.
 * nas_frame_free remains the compatibility alias for put. */
bool nas_frame_get(uint64_t);
bool nas_frame_put(uint64_t);
uint32_t nas_frame_refs(uint64_t);
bool nas_frame_free(uint64_t);
uint64_t nas_hhdm_offset(void);
#endif
