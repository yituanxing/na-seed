#ifndef NAS_UBOOT_BASE_H
#define NAS_UBOOT_BASE_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "../../../kernel/errno.h"
#include "../../../kernel/lib/string.h"
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
typedef uint64_t u64; typedef unsigned long ulong; typedef uint64_t lbaint_t;
typedef uint8_t __u8;typedef uint16_t __u16;typedef uint32_t __u32;typedef uint64_t __u64;
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#define UCLASS_VIRTIO 1
#define UCLASS_BLK 2
#define PART_TYPE_UNKNOWN 0
#define DM_FLAG_ACTIVE_DMA 1
#define VIRTIO_BLK_DRV_NAME "virtio-blk"
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define min(a,b) ((a)<(b)?(a):(b))
#define log_debug(...) do{}while(0)
int nas_uboot_sprintf(char *,const char *,...);
#define sprintf nas_uboot_sprintf
#endif
