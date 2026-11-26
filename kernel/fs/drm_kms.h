#ifndef NASEED_DRM_KMS_H
#define NASEED_DRM_KMS_H
#include <stdint.h>
int nas_drm_kms_open(unsigned slot);
void nas_drm_kms_release(unsigned slot);
/* Return -ENOTTY only for an ioctl not owned by KMS. */
uint64_t nas_drm_kms_ioctl(unsigned slot,uint32_t cmd,uint64_t arg);
#endif
