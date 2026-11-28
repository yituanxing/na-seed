#ifndef NASEED_DRM_KMS_H
#define NASEED_DRM_KMS_H
#include <stdint.h>
#include <stddef.h>
int nas_drm_kms_open(unsigned slot);
void nas_drm_kms_release(unsigned slot);
/* Return -ENOTTY only for an ioctl not owned by KMS. */
uint64_t nas_drm_kms_ioctl(unsigned slot,uint32_t cmd,uint64_t arg);
/* Native Linux DRM variable-length event FIFO, wired through FileOps. */
uint64_t nas_drm_kms_read_plan(unsigned slot,uint64_t requested);
uint64_t nas_drm_kms_read(unsigned slot,uint8_t *dst,uint64_t bytes);
uint16_t nas_drm_kms_poll(unsigned slot,uint16_t requested);
/* Called by the already registered GPU scheduler service, after transport
 * IRQ completion processing. No poll, usercopy or blocking in this callback. */
int nas_drm_kms_deferred_step(void);
#endif
