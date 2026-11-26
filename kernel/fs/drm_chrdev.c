/* Linux DRM primary character device. Device discovery is owned by the
 * common chrdev registry; VirtIO GPU remains the sole hardware owner.
 * Keep user pointers and Linux ioctl wire structs out of the GPU driver.
 * Do not claim unimplemented GEM/KMS capabilities merely to start Weston. */
#include "drm_chrdev.h"
#include "../drivers/core/chrdev.h"
#include "../drivers/display/virtio_gpu.h"
#include "../fd.h"
#include "../task.h"
#include "../errno.h"
#include "../mm/usercopy.h"
#include "../lib/string.h"
#include <stdint.h>
#include <stddef.h>

#define NAS_DRM_IOCTL_VERSION UINT32_C(0xc0406400)
#define NAS_DRM_IOCTL_GET_CAP UINT32_C(0xc010640c)

/* Native Linux LP64 UAPI; checked independently of the compiler's padding. */
struct nas_drm_version {
    int32_t major,minor,patchlevel;
    uint32_t padding;
    uint64_t name_len,name,date_len,date,desc_len,desc;
};
struct nas_drm_get_cap {uint64_t capability,value;};
_Static_assert(sizeof(struct nas_drm_version)==64U,"DRM LP64 version");
_Static_assert(sizeof(struct nas_drm_get_cap)==16U,"DRM get_cap");

static bool drm_present(unsigned unit) {
    return unit==0U && nas_virtio_gpu_count()!=0U;
}
static struct nas_chrdev_node drm_node={
    .path="/dev/dri/card0",
    .info={
        .fd_kind=FD_DRM,.unit=0,.dev=2,.ino=3060,
        .rdev=(226U<<8),.mode=0020600U
    },
    .present=drm_present
};
int nas_drm_chrdev_init(void) {
    return nas_chrdev_node_register(&drm_node);
}
static int copy_string(uint64_t user_addr,uint64_t capacity,
                       const char *text,uint64_t length) {
    if(!user_addr || !capacity || !length)return 0;
    uint64_t count=capacity<length?capacity:length;
    return nas_user_write_bytes(user_addr,(const uint8_t *)text,count) ?
           0:-EFAULT;
}
static uint64_t drm_version(uint64_t user_arg) {
    struct nas_drm_version version;
    if(!nas_user_read_bytes(user_arg,(uint8_t *)&version,sizeof(version)))
        return (uint64_t)-EFAULT;
    /* Linux virtio_gpu exposes the original driver name used by libdrm.
     * The date field is intentionally empty: no invented upstream date. */
    static const char name[]="virtio_gpu";
    static const char description[]="VirtIO GPU 2D";
    int rc=copy_string(version.name,version.name_len,
                       name,sizeof(name)-1U);
    if(!rc)rc=copy_string(version.desc,version.desc_len,
                          description,sizeof(description)-1U);
    if(rc)return (uint64_t)(int64_t)rc;
    version.major=0;
    version.minor=1;
    version.patchlevel=0;
    version.name_len=sizeof(name)-1U;
    version.date_len=0;
    version.desc_len=sizeof(description)-1U;
    return nas_user_write_bytes(user_arg,(const uint8_t *)&version,
                                sizeof(version))?0:(uint64_t)-EFAULT;
}
static uint64_t drm_cap(uint64_t user_arg) {
    struct nas_drm_get_cap cap;
    if(!nas_user_read_bytes(user_arg,(uint8_t *)&cap,sizeof(cap)))
        return (uint64_t)-EFAULT;
    /* Every listed capability is currently unavailable. In particular,
     * dumb-buffer support cannot be true before GEM allocation exists. */
    switch(cap.capability) {
    case 0x01U: /* DRM_CAP_DUMB_BUFFER */
    case 0x02U: /* DRM_CAP_VBLANK_HIGH_CRTC */
    case 0x03U: /* DRM_CAP_DUMB_PREFERRED_DEPTH */
    case 0x04U: /* DRM_CAP_DUMB_PREFER_SHADOW */
    case 0x05U: /* DRM_CAP_PRIME */
    case 0x06U: /* DRM_CAP_TIMESTAMP_MONOTONIC */
    case 0x07U: /* DRM_CAP_ASYNC_PAGE_FLIP */
    case 0x08U: /* DRM_CAP_CURSOR_WIDTH */
    case 0x09U: /* DRM_CAP_CURSOR_HEIGHT */
    case 0x10U: /* DRM_CAP_ADDFB2_MODIFIERS */
    case 0x11U: /* DRM_CAP_PAGE_FLIP_TARGET */
    case 0x12U: /* DRM_CAP_CRTC_IN_VBLANK_EVENT */
    case 0x13U: /* DRM_CAP_SYNCOBJ */
    case 0x14U: /* DRM_CAP_SYNCOBJ_TIMELINE */
        cap.value=0;
        break;
    default:
        return (uint64_t)-EINVAL;
    }
    return nas_user_write_bytes(user_arg,(const uint8_t *)&cap,
                                sizeof(cap))?0:(uint64_t)-EFAULT;
}
uint64_t nas_drm_chrdev_ioctl(uint64_t fd,uint64_t request,uint64_t user_arg) {
    if(fd>=NAS_FD_LIMIT ||
       nas_current_task->fds->entries[fd].kind!=FD_DRM)
        return (uint64_t)-EBADF;
    if(!drm_present(0))return (uint64_t)-ENODEV;
    if(request==NAS_DRM_IOCTL_VERSION)return drm_version(user_arg);
    if(request==NAS_DRM_IOCTL_GET_CAP)return drm_cap(user_arg);
    return (uint64_t)-ENOTTY;
}
