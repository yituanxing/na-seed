#ifndef NASEED_DRM_CHRDEV_H
#define NASEED_DRM_CHRDEV_H
#include <stdint.h>
struct nas_vm_device;

/* Linux DRM primary-node personality for the existing VirtIO GPU owner.
 * This is only the identification ABI. GEM/KMS buffer ownership and
 * modesetting have not been published and must not be claimed as supported. */
int nas_drm_chrdev_init(void);
uint64_t nas_drm_chrdev_ioctl(uint64_t fd,uint64_t request,uint64_t user_arg);
void nas_drm_chrdev_release(unsigned slot);
int nas_drm_chrdev_mmap_pin(unsigned slot,uint64_t offset,
                            struct nas_vm_device **out);

#endif
