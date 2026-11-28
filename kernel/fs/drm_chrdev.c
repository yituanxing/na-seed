/* Linux DRM primary character device. Device discovery is owned by the
 * common chrdev registry; VirtIO GPU remains the sole hardware owner.
 * Keep user pointers and Linux ioctl wire structs out of the GPU driver.
 * Do not claim unimplemented GEM/KMS capabilities merely to start Weston. */
#include "drm_chrdev.h"
#include "drm_gem.h"
#include "drm_kms.h"
#include "../drivers/core/chrdev.h"
#include "../drivers/display/virtio_gpu.h"
#include "../fd.h"
#include "../task.h"
#include "../errno.h"
#include "../mm/usercopy.h"
#include "../lib/string.h"
#include "../drivers/serial.h"
#include <stdint.h>
#include <stddef.h>

#define NAS_DRM_IOCTL_VERSION UINT32_C(0xc0406400)
#define NAS_DRM_IOCTL_GET_CAP UINT32_C(0xc010640c)
#define NAS_DRM_IOCTL_GEM_CLOSE UINT32_C(0x40086409)
#define NAS_DRM_IOCTL_MODE_CREATE_DUMB UINT32_C(0xc02064b2)
#define NAS_DRM_IOCTL_MODE_MAP_DUMB UINT32_C(0xc01064b3)
#define NAS_DRM_IOCTL_MODE_DESTROY_DUMB UINT32_C(0xc00464b4)

/* Native Linux LP64 UAPI; checked independently of the compiler's padding. */
struct nas_drm_version {
    int32_t major,minor,patchlevel;
    uint32_t padding;
    uint64_t name_len,name,date_len,date,desc_len,desc;
};
struct nas_drm_get_cap {uint64_t capability,value;};
struct nas_drm_mode_create_dumb {
    uint32_t height,width,bpp,flags,handle,pitch;
    uint64_t size;
};
struct nas_drm_mode_map_dumb {uint32_t handle,pad;uint64_t offset;};
struct nas_drm_mode_destroy_dumb {uint32_t handle;};
struct nas_drm_gem_close {uint32_t handle,pad;};
_Static_assert(sizeof(struct nas_drm_version)==64U,"DRM LP64 version");
_Static_assert(sizeof(struct nas_drm_get_cap)==16U,"DRM get_cap");
_Static_assert(sizeof(struct nas_drm_mode_create_dumb)==32U,"DRM dumb create");
_Static_assert(sizeof(struct nas_drm_mode_map_dumb)==16U,"DRM dumb map");
_Static_assert(sizeof(struct nas_drm_mode_destroy_dumb)==4U,"DRM dumb destroy");
_Static_assert(sizeof(struct nas_drm_gem_close)==8U,"DRM GEM close");

static bool drm_present(unsigned unit) {
    return unit==0U && nas_virtio_gpu_count()!=0U;
}
static int drm_file_open(unsigned slot,unsigned unit) {
    if(!drm_present(unit))return -ENODEV;
    return nas_drm_kms_open(slot);
}
static const struct nas_chrdev_provider drm_provider={
    .name="linux-drm-primary",.open=drm_file_open
};
static struct nas_chrdev_node drm_node={
    .path="/dev/dri/card0",
    .info={
        .fd_kind=FD_DRM,.unit=0,.dev=2,.ino=3060,
        .rdev=(226U<<8),.mode=0020600U
    },
    .present=drm_present,.provider=&drm_provider
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
    /* Report the actual GEM allocation and completion-clock semantics.
     * PAGE_FLIP events use the architecture's monotonic nanosecond clock. */
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
        cap.value=(cap.capability==0x01U || cap.capability==0x06U) ? 1U : 0U;
        break;
    default:
        return (uint64_t)-EINVAL;
    }
    return nas_user_write_bytes(user_arg,(const uint8_t *)&cap,
                                sizeof(cap))?0:(uint64_t)-EFAULT;
}
static uint64_t dumb_create(unsigned slot,uint64_t arg) {
    struct nas_drm_mode_create_dumb req;
    if(!nas_user_read_bytes(arg,(uint8_t *)&req,sizeof(req)))
        return (uint64_t)-EFAULT;
    /* Validate writable userspace BEFORE reserving physical frames. */
    if(!nas_user_prepare_write(arg,sizeof(req)))return (uint64_t)-EFAULT;
    struct nas_drm_gem_created new_object;
    int rc=nas_drm_gem_create(slot,req.width,req.height,req.bpp,
                              req.flags,&new_object);
    /* Diagnose original Xorg allocator negotiation before treating the
     * subsequent NULL dumb_bo as a caller or MM failure. Never fake IOCTL
     * results, and retain normal GEM ownership and errno semantics. */
    nas_print("[DRM DUMB] width=");nas_dec(req.width);
    nas_print(" height=");nas_dec(req.height);
    nas_print(" bpp=");nas_dec(req.bpp);
    nas_print(" flags=");nas_dec(req.flags);
    nas_print(" rc=");nas_dec(rc<0?(uint64_t)(-(int64_t)rc):0);
    nas_print(" handle=");nas_dec(rc?0:new_object.handle);
    nas_print("\n");
    if(rc)return (uint64_t)(int64_t)rc;
    req.handle=new_object.handle;
    req.pitch=new_object.pitch;
    req.size=new_object.size;
    if(nas_user_write_bytes(arg,(const uint8_t *)&req,sizeof(req)))return 0;
    (void)nas_drm_gem_destroy(slot,new_object.handle);
    return (uint64_t)-EFAULT;
}
static uint64_t dumb_map(unsigned slot,uint64_t arg) {
    struct nas_drm_mode_map_dumb req;
    if(!nas_user_read_bytes(arg,(uint8_t *)&req,sizeof(req)))
        return (uint64_t)-EFAULT;
    if(req.pad)return (uint64_t)-EINVAL;
    uint64_t offset=0;
    int rc=nas_drm_gem_map_offset(slot,req.handle,&offset);
    if(rc)return (uint64_t)(int64_t)rc;
    req.offset=offset;
    return nas_user_write_bytes(arg,(const uint8_t *)&req,sizeof(req)) ?
           0:(uint64_t)-EFAULT;
}
static uint64_t dumb_destroy(unsigned slot,uint64_t arg,bool gem_close) {
    uint32_t handle=0;
    if(gem_close) {
        struct nas_drm_gem_close req;
        if(!nas_user_read_bytes(arg,(uint8_t *)&req,sizeof(req)))
            return (uint64_t)-EFAULT;
        if(req.pad)return (uint64_t)-EINVAL;
        handle=req.handle;
    } else {
        struct nas_drm_mode_destroy_dumb req;
        if(!nas_user_read_bytes(arg,(uint8_t *)&req,sizeof(req)))
            return (uint64_t)-EFAULT;
        handle=req.handle;
    }
    return (uint64_t)(int64_t)nas_drm_gem_destroy(slot,handle);
}
int nas_drm_chrdev_mmap_pin(unsigned slot,uint64_t offset,
                            struct nas_vm_device **out) {
    if(!drm_present(0))return -ENODEV;
    return nas_drm_gem_mmap_pin(slot,offset,out);
}
void nas_drm_chrdev_release(unsigned slot) {
    nas_drm_kms_release(slot);
    nas_drm_gem_file_release(slot);
}
uint64_t nas_drm_chrdev_ioctl(uint64_t fd,uint64_t request,uint64_t user_arg) {
    if(fd>=NAS_FD_LIMIT ||
       nas_current_task->fds->entries[fd].kind!=FD_DRM)
        return (uint64_t)-EBADF;
    if(!drm_present(0))return (uint64_t)-ENODEV;
    unsigned slot=nas_current_task->fds->entries[fd].slot;
    if(request==NAS_DRM_IOCTL_VERSION)return drm_version(user_arg);
    if(request==NAS_DRM_IOCTL_GET_CAP)return drm_cap(user_arg);
    if(request==NAS_DRM_IOCTL_MODE_CREATE_DUMB)
        return dumb_create(slot,user_arg);
    if(request==NAS_DRM_IOCTL_MODE_MAP_DUMB)
        return dumb_map(slot,user_arg);
    if(request==NAS_DRM_IOCTL_MODE_DESTROY_DUMB)
        return dumb_destroy(slot,user_arg,false);
    if(request==NAS_DRM_IOCTL_GEM_CLOSE)
        return dumb_destroy(slot,user_arg,true);
    return nas_drm_kms_ioctl(slot,(uint32_t)request,user_arg);
}
