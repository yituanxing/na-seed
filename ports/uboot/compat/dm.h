#ifndef NAS_UBOOT_DM_H
#define NAS_UBOOT_DM_H
#include "blk.h"
struct udevice {
 const char *name;
 struct udevice *parent;
 void *priv;
 void *uclass_priv;
 void *plat; /* UCLASS_BLK blk_desc or UCLASS_ETH eth_pdata */
 void *context;
};
struct uboot_driver{
 const char *name; int id;
 const void *ops; /* class-specific ops, not a second U-Boot device model */
 int (*bind)(struct udevice *);
 int (*probe)(struct udevice *);
 int (*remove)(struct udevice *);
 size_t priv_auto;
 size_t plat_auto;
 int flags;
};
#define U_BOOT_DRIVER(x) static const struct uboot_driver uboot_##x
static inline void *dev_get_priv(struct udevice *d){return d->priv;}
static inline void *dev_get_uclass_priv(struct udevice *d){return d->uclass_priv;}
static inline void *dev_get_plat(struct udevice *d){return d->plat;}
static inline struct blk_desc *dev_get_uclass_plat(struct udevice *d){return (struct blk_desc *)d->plat;}
static inline bool device_active(struct udevice *d){return d && d->priv;}
#endif
