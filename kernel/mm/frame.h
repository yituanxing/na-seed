#ifndef NASEED_FRAME_H
#define NASEED_FRAME_H
#include <stdint.h>
#include <stdbool.h>
struct limine_memmap_response;
bool nas_frames_init(const struct limine_memmap_response *, uint64_t);
uint64_t nas_free_frames(void);
uint64_t nas_managed_frames(void);
/* Read-only sysfs memory-block topology, derived from Limine's real usable
 * physical ranges. The block granularity is an OS-visible 128 MiB unit;
 * sparse physical address gaps do not create phantom memoryN directories. */
#define NAS_MEMORY_BLOCK_BYTES (128ULL * 1024ULL * 1024ULL)
bool nas_memory_block_present(uint64_t index);
bool nas_memory_block_index_at(uint64_t ordinal, uint64_t *index);
uint64_t nas_frame_alloc(void);
/* All-or-nothing reservation of adjacent 4 KiB usable frames, zeroed and
 * returned with one reference each. A zero result leaves state unchanged. */
uint64_t nas_frame_alloc_contiguous(uint64_t pages);
/* Allocate grants one owner; get increments; put drops only one reference.
 * The final put returns the 4 KiB frame to the free bitmap. No separate
 * unconditional 'free' API exists: mapped pages can have multiple owners. */
bool nas_frame_get(uint64_t);
bool nas_frame_put(uint64_t);
uint32_t nas_frame_refs(uint64_t);
uint64_t nas_hhdm_offset(void);
#endif
