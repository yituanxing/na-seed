#ifndef NAS_UBOOT_BLK_H
#define NAS_UBOOT_BLK_H
#include "base.h"
struct udevice;
struct blk_desc{
 int uclass_id,devnum,part_type,log2blksz;
 char vendor[64];
 struct udevice *bdev;
 uint64_t lba;
 unsigned long blksz;
};
struct blk_ops{
 ulong (*read)(struct udevice *,lbaint_t,lbaint_t,void *);
 ulong (*write)(struct udevice *,lbaint_t,lbaint_t,const void *);
 ulong (*erase)(struct udevice *,lbaint_t,lbaint_t);
};
int blk_next_free_devnum(int);
#endif
