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
#include "../mm/heap.h"
#include "../lib/string.h"
#include "../drivers/serial.h"
#include <stdint.h>
#include <stddef.h>

#if defined(NAS_EXPERIMENT_TGOS_DRM_FFI)
/* The source of this symbol is ORIGINAL TGOSKits DRM code called through
 * a Rust/C staticlib bridge, not a self-written alternate DRM driver. */
extern int nas_tgos_drm_check_original_abi(void);
#endif

#define NAS_DRM_IOCTL_VERSION UINT32_C(0xc0406400)
#if defined(NAS_EXPERIMENT_TGOS_GPU_OWNER)
/* Exact Linux UAPI ioctl numbers: /uapi/drm/virtgpu_drm.h.
 * Only original GPU capset discovery is implemented. There is deliberately
 * no fake GEM, EXECBUFFER, KMS or OpenGL 3D advertising. */
#define NAS_DRM_IOCTL_VIRTGPU_MAP UINT32_C(0xc0106441)
#define NAS_DRM_IOCTL_VIRTGPU_EXECBUFFER UINT32_C(0xc0406442)
#define NAS_DRM_IOCTL_VIRTGPU_RESOURCE_CREATE UINT32_C(0xc0386444)
#define NAS_DRM_IOCTL_VIRTGPU_RESOURCE_INFO UINT32_C(0xc0106445)
#define NAS_DRM_IOCTL_VIRTGPU_GETPARAM UINT32_C(0xc0106443)
#define NAS_DRM_IOCTL_VIRTGPU_GET_CAPS UINT32_C(0xc0186449)
#define NAS_TGOS_MAX_CAPSET_BYTES 65536U
extern unsigned nas_tgos_original_gpu_active(void);
extern int nas_tgos_original_gpu_render_open(uint32_t slot);
extern int nas_tgos_original_gpu_render_close(uint32_t slot);
extern int nas_tgos_original_gpu_render_exec_nop(uint32_t slot,
                                    const uint8_t *command,size_t bytes,
                                    uint32_t bo_handle);
struct nas_drm_virtgpu_resource_create;
extern int nas_tgos_original_gpu_render_resource_create(uint32_t slot,
                  const struct nas_drm_virtgpu_resource_create *request,
                  uint32_t *handle,uint32_t *resource);
extern int nas_tgos_original_gpu_render_resource_info(uint32_t slot,
                   uint32_t handle,uint32_t *resource,uint32_t *size);
extern int nas_tgos_original_gpu_render_resource_close(uint32_t slot,uint32_t handle);
extern int nas_tgos_original_gpu_get_param(uint64_t param,uint64_t *value);
extern int nas_tgos_original_gpu_get_caps(uint32_t id,uint32_t version,
                                          uint8_t *out,size_t size,size_t *written);
#endif
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
#if defined(NAS_EXPERIMENT_TGOS_GPU_OWNER)
/* Exact pinned TGOSKits axdrm VirtGPU UAPI field order and LP64 size. */
struct nas_drm_virtgpu_execbuffer {
    uint32_t flags,size;
    uint64_t command,bo_handles;
    uint32_t num_bo_handles;
    int32_t fence_fd;
    uint32_t ring_idx,syncobj_stride,num_in_syncobjs,num_out_syncobjs;
    uint64_t in_syncobjs,out_syncobjs;
};
_Static_assert(sizeof(struct nas_drm_virtgpu_execbuffer)==64U,
               "Linux virtgpu EXECBUFFER LP64 ABI");
struct nas_drm_virtgpu_map {
    uint64_t offset;
    uint32_t handle,pad;
};
_Static_assert(sizeof(struct nas_drm_virtgpu_map)==16U,
               "Linux VirtGPU MAP LP64 ABI");
struct nas_drm_virtgpu_resource_create {
    uint32_t target,format,bind,width,height,depth,array_size,last_level;
    uint32_t nr_samples,flags,bo_handle,res_handle,size,stride;
};
struct nas_drm_virtgpu_resource_info {
    uint32_t bo_handle,res_handle,size,blob_mem;
};
_Static_assert(sizeof(struct nas_drm_virtgpu_resource_create)==56U,
               "Linux VirtGPU RESOURCE_CREATE LP64 ABI");
_Static_assert(sizeof(struct nas_drm_virtgpu_resource_info)==16U,
               "Linux VirtGPU RESOURCE_INFO LP64 ABI");
struct nas_drm_virtgpu_getparam {uint64_t param,value;};
struct nas_drm_virtgpu_get_caps {
    uint32_t cap_set_id,cap_set_ver;
    uint64_t addr;
    uint32_t size,pad;
};
_Static_assert(sizeof(struct nas_drm_virtgpu_getparam)==16U,
               "Linux virtgpu GETPARAM LP64 ABI");
_Static_assert(sizeof(struct nas_drm_virtgpu_get_caps)==24U,
               "Linux virtgpu GET_CAPS LP64 ABI");
#endif
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
#if defined(NAS_EXPERIMENT_TGOS_GPU_OWNER)
    return unit==128U && nas_tgos_original_gpu_active()!=0U;
#else
    return unit==0U && nas_virtio_gpu_count()!=0U;
#endif
}
static int drm_file_open(unsigned slot,unsigned unit) {
    if(!drm_present(unit))return -ENODEV;
#if defined(NAS_EXPERIMENT_TGOS_GPU_OWNER)
    if(unit==128U)
        return nas_tgos_original_gpu_render_open((uint32_t)slot);
#endif
    return nas_drm_kms_open(slot);
}
static const struct nas_chrdev_provider drm_provider={
    .name="linux-drm-primary",.open=drm_file_open
};
#if defined(NAS_EXPERIMENT_TGOS_GPU_OWNER)
/* This is the live Rust GPU owner, not a card0 alias or a second driver.
 * Only the explicit feature-gated experimental kernel publishes this node. */
static struct nas_chrdev_node render_node={
    .path="/dev/dri/renderD128",
    .info={
        .fd_kind=FD_DRM,.unit=128,.dev=2,.ino=3061,
        .rdev=(226U<<8)|128U,.mode=0020600U
    },
    .present=drm_present,.provider=&drm_provider
};
static bool render_file(unsigned slot) {
    return slot<NAS_OPEN_FILE_LIMIT &&
           nas_open_files[slot].chrdev_node==&render_node;
}
bool nas_drm_tgos_render_busy(void) {
    return render_node.pins!=0U;
}
#endif
#if !defined(NAS_EXPERIMENT_TGOS_GPU_OWNER)
static struct nas_chrdev_node drm_node={
    .path="/dev/dri/card0",
    .info={
        .fd_kind=FD_DRM,.unit=0,.dev=2,.ino=3060,
        .rdev=(226U<<8),.mode=0020600U
    },
    .present=drm_present,.provider=&drm_provider
};
#endif
int nas_drm_chrdev_init(void) {
#if defined(NAS_EXPERIMENT_TGOS_DRM_FFI)
    /* This opt-in experiment must execute real original upstream Rust
     * symbols IN the NaSeed kernel. Refuse DRM publication on mismatch.
     * It does not provide a new GPU owner, render node or 3D ioctl. */
    int abi_rc=nas_tgos_drm_check_original_abi();
    if(abi_rc) {
        nas_print("[TGOS ORIGINAL DRM ABI FAIL]\n");
        return abi_rc;
    }
    /* Emitted AFTER the native C code has called and verified the actual
     * original TGOSKits DRM FOURCC and DRM-KAPI provider-slot functions. */
    nas_print("[TGOS ORIGINAL DRM ABI PASS]\n");
#endif
#if defined(NAS_EXPERIMENT_TGOS_GPU_OWNER)
    /* The old card0 would not have a real scanout owner here; do not make
     * a dead card0 visible or advertise imaginary KMS capabilities. */
    return nas_chrdev_node_register(&render_node);
#else
    return nas_chrdev_node_register(&drm_node);
#endif
}
static int copy_string(uint64_t user_addr,uint64_t capacity,
                       const char *text,uint64_t length) {
    if(!user_addr || !capacity || !length)return 0;
    uint64_t count=capacity<length?capacity:length;
    return nas_user_write_bytes(user_addr,(const uint8_t *)text,count) ?
           0:-EFAULT;
}
static uint64_t drm_version(uint64_t user_arg,bool is_render) {
    struct nas_drm_version version;
    if(!nas_user_read_bytes(user_arg,(uint8_t *)&version,sizeof(version)))
        return (uint64_t)-EFAULT;
    /* Linux virtio_gpu exposes the original driver name used by libdrm.
     * The date field is intentionally empty: no invented upstream date. */
    static const char name[]="virtio_gpu";
    static const char description[]="VirtIO GPU 2D";
#if defined(NAS_EXPERIMENT_TGOS_GPU_OWNER)
    static const char render_description[]=
        "TGOSKits VirtIO-GPU / live VirGL capset (GEM/exec not wired)";
    const char *desc=is_render?render_description:description;
    const uint64_t desc_len=is_render?(sizeof(render_description)-1U):
                                   (sizeof(description)-1U);
#else
    (void)is_render;
    const char *desc=description;
    const uint64_t desc_len=sizeof(description)-1U;
#endif
    int rc=copy_string(version.name,version.name_len,
                       name,sizeof(name)-1U);
    if(!rc)rc=copy_string(version.desc,version.desc_len,desc,desc_len);
    if(rc)return (uint64_t)(int64_t)rc;
    version.major=0;
    version.minor=1;
    version.patchlevel=0;
    version.name_len=sizeof(name)-1U;
    version.date_len=0;
    version.desc_len=desc_len;
    return nas_user_write_bytes(user_arg,(const uint8_t *)&version,
                                sizeof(version))?0:(uint64_t)-EFAULT;
}
static uint64_t drm_cap(uint64_t user_arg,bool is_render) {
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
    case 0x10U: /* DRM_CAP_ADDFB2_MODIFIERS */
    case 0x11U: /* DRM_CAP_PAGE_FLIP_TARGET */
    case 0x12U: /* DRM_CAP_CRTC_IN_VBLANK_EVENT */
    case 0x13U: /* DRM_CAP_SYNCOBJ */
    case 0x14U: /* DRM_CAP_SYNCOBJ_TIMELINE */
        cap.value=(cap.capability==0x06U ||
                   (!is_render && cap.capability==0x01U)) ? 1U : 0U;
        break;
    case 0x08U: /* DRM_CAP_CURSOR_WIDTH */
    case 0x09U: /* DRM_CAP_CURSOR_HEIGHT */
        /* The native DRM facade does not expose hardware-cursor ioctls.
         * Reporting successful capabilities with zero-size cursors makes
         * upstream Xorg allocate a zero-byte cursor BO and dereference NULL
         * during drmmode_map_cursor_bos. Unsupported caps must fail, so
         * libdrm/Xorg retain their standard fallback dimensions. */
        return (uint64_t)-EINVAL;
    default:
        return (uint64_t)-EINVAL;
    }
    return nas_user_write_bytes(user_arg,(const uint8_t *)&cap,
                                sizeof(cap))?0:(uint64_t)-EFAULT;
}
#if defined(NAS_EXPERIMENT_TGOS_GPU_OWNER)
/* Linux virtgpu GETPARAM carries a u64 user pointer to a C int,
 * NOT to a u64-sized result (as upstream TGOSKits ioctl_getparam proves).
 * Copying eight bytes corrupts the adjacent userspace object. */
static uint64_t render_getparam(uint64_t arg) {
    struct nas_drm_virtgpu_getparam req;
    if(!nas_user_read_bytes(arg,(uint8_t *)&req,sizeof(req)))
        return (uint64_t)-EFAULT;
    if(!req.value || !nas_user_prepare_write(req.value,sizeof(uint32_t)))
        return (uint64_t)-EFAULT;
    uint64_t original_value=0;
    int rc=nas_tgos_original_gpu_get_param(req.param,&original_value);
    if(rc)return (uint64_t)(int64_t)rc;
    /* Match Linux's 32-bit result, including the capset-ID mask truncation. */
    uint32_t value=(uint32_t)original_value;
    return nas_user_write_bytes(req.value,(const uint8_t *)&value,
                                sizeof(value))?0:(uint64_t)-EFAULT;
}
/* Original VirtIoGpu handles actual nonempty user command submission. The
 * limited gate now validates a single file-owned BO handle; arbitrary VirGL
 * command streams, mmap and syncobj/fence-fd still require upstream axdrm. */
static uint64_t render_execbuffer(unsigned slot,uint64_t arg) {
    struct nas_drm_virtgpu_execbuffer req;
    if(!nas_user_read_bytes(arg,(uint8_t *)&req,sizeof(req)))
        return (uint64_t)-EFAULT;
    if(req.flags || req.num_bo_handles>1U ||
       (req.num_bo_handles==0U && req.bo_handles!=0) ||
       (req.num_bo_handles==1U && req.bo_handles==0) ||
       req.ring_idx || req.syncobj_stride ||
       req.num_in_syncobjs || req.num_out_syncobjs ||
       req.in_syncobjs || req.out_syncobjs)
        return (uint64_t)-EOPNOTSUPP;
    if(req.size!=sizeof(uint32_t) || !req.command)
        return (uint64_t)-EOPNOTSUPP;
    uint32_t nop_header=UINT32_MAX;
    if(!nas_user_read_bytes(req.command,(uint8_t *)&nop_header,
                            sizeof(nop_header)))
        return (uint64_t)-EFAULT;
    uint32_t bo_handle=0;
    if(req.num_bo_handles==1U &&
       !nas_user_read_bytes(req.bo_handles,(uint8_t *)&bo_handle,
                            sizeof(bo_handle)))
        return (uint64_t)-EFAULT;
    return (uint64_t)(int64_t)
        nas_tgos_original_gpu_render_exec_nop((uint32_t)slot,
               (const uint8_t *)&nop_header,sizeof(nop_header),bo_handle);
}
/* Linux VirtGPU resource ioctls. The NaSeed VFS side only validates and
 * copies user pointers; ALL real GPU commands go through pinned TGOSKits.
 * This is a host 3D-object gate, not completed mmap-backed GEM or Mesa.
 */
static uint64_t render_resource_create(unsigned slot,uint64_t arg) {
    struct nas_drm_virtgpu_resource_create req;
    if(!nas_user_read_bytes(arg,(uint8_t *)&req,sizeof(req)))
        return (uint64_t)-EFAULT;
    if(!nas_user_prepare_write(arg,sizeof(req)))
        return (uint64_t)-EFAULT;
    uint32_t handle=0,resource=0;
    int rc=nas_tgos_original_gpu_render_resource_create((uint32_t)slot,
                                                    &req,&handle,&resource);
    if(rc)return (uint64_t)(int64_t)rc;
    req.bo_handle=handle;
    req.res_handle=resource;
    if(nas_user_write_bytes(arg,(const uint8_t *)&req,sizeof(req)))return 0;
    /* User mappings can disappear under concurrent unmap; rollback the BO. */
    (void)nas_tgos_original_gpu_render_resource_close((uint32_t)slot,handle);
    return (uint64_t)-EFAULT;
}
/* Exact Linux VirtGPU MAP ioctl; return EXISTING NaSeed GEM mmap
 * cookie. The VMA's independent object refcount protects physical pages
 * if the user closes the BO while a mapping is still live. */
static uint64_t render_map(unsigned slot,uint64_t arg) {
    struct nas_drm_virtgpu_map req;
    if(!nas_user_read_bytes(arg,(uint8_t *)&req,sizeof(req)))
        return (uint64_t)-EFAULT;
    if(req.pad)return (uint64_t)-EINVAL;
    if(!nas_user_prepare_write(arg,sizeof(req)))
        return (uint64_t)-EFAULT;
    uint64_t offset=0;
    int rc=nas_drm_gem_map_offset(slot,req.handle,&offset);
    if(rc)return (uint64_t)(int64_t)rc;
    req.offset=offset;
    return nas_user_write_bytes(arg,(const uint8_t *)&req,sizeof(req))?
           0:(uint64_t)-EFAULT;
}
static uint64_t render_resource_info(unsigned slot,uint64_t arg) {
    struct nas_drm_virtgpu_resource_info req;
    if(!nas_user_read_bytes(arg,(uint8_t *)&req,sizeof(req)))
        return (uint64_t)-EFAULT;
    if(!nas_user_prepare_write(arg,sizeof(req)))
        return (uint64_t)-EFAULT;
    uint32_t res=0,size=0;
    int rc=nas_tgos_original_gpu_render_resource_info((uint32_t)slot,
                                                req.bo_handle,&res,&size);
    if(rc)return (uint64_t)(int64_t)rc;
    req.res_handle=res;
    req.size=size;
    req.blob_mem=0;
    return nas_user_write_bytes(arg,(const uint8_t *)&req,sizeof(req))?
           0:(uint64_t)-EFAULT;
}
static uint64_t render_gem_close(unsigned slot,uint64_t arg) {
    struct nas_drm_gem_close req;
    if(!nas_user_read_bytes(arg,(uint8_t *)&req,sizeof(req)))
        return (uint64_t)-EFAULT;
    if(req.pad)return (uint64_t)-EINVAL;
    return (uint64_t)(int64_t)
        nas_tgos_original_gpu_render_resource_close((uint32_t)slot,
                                                     req.handle);
}
static uint64_t render_getcaps(uint64_t arg) {
    struct nas_drm_virtgpu_get_caps req;
    if(!nas_user_read_bytes(arg,(uint8_t *)&req,sizeof(req)))
        return (uint64_t)-EFAULT;
    if(!req.addr || !req.size || req.size>NAS_TGOS_MAX_CAPSET_BYTES ||
       req.pad)return (uint64_t)-EINVAL;
    if(!nas_user_prepare_write(req.addr,req.size))
        return (uint64_t)-EFAULT;
    uint8_t *data=nas_heap_alloc(req.size);
    if(!data)return (uint64_t)-ENOMEM;
    size_t written=0;
    int rc=nas_tgos_original_gpu_get_caps(req.cap_set_id,req.cap_set_ver,
                                          data,req.size,&written);
    bool copied=!rc && written>0 && written<=req.size &&
                nas_user_write_bytes(req.addr,data,written);
    if(!nas_heap_free(data))__builtin_trap();
    if(rc)return (uint64_t)(int64_t)rc;
    return copied?0:(uint64_t)-EFAULT;
}
#endif

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
#if defined(NAS_EXPERIMENT_TGOS_GPU_OWNER)
    if(render_file(slot))
        return nas_drm_gem_mmap_pin(slot,offset,out);
#endif
    if(!drm_present(0))return -ENODEV;
    return nas_drm_gem_mmap_pin(slot,offset,out);
}
void nas_drm_chrdev_release(unsigned slot) {
#if defined(NAS_EXPERIMENT_TGOS_GPU_OWNER)
    if(render_file(slot)) {
        int rc=nas_tgos_original_gpu_render_close((uint32_t)slot);
        if(rc) {
            nas_print("[TGOS RENDER CONTEXT RETIRE FAIL] code=");
            nas_dec((uint64_t)(-(int64_t)rc));nas_print("\n");
        }
        return; /* Owns no KMS/GEM allocations yet. */
    }
#endif
    nas_drm_kms_release(slot);
    nas_drm_gem_file_release(slot);
}
uint64_t nas_drm_chrdev_ioctl(uint64_t fd,uint64_t request,uint64_t user_arg) {
    if(fd>=NAS_FD_LIMIT ||
       nas_current_task->fds->entries[fd].kind!=FD_DRM)
        return (uint64_t)-EBADF;
    unsigned slot=nas_current_task->fds->entries[fd].slot;
#if defined(NAS_EXPERIMENT_TGOS_GPU_OWNER)
    if(render_file(slot)) {
        if(!drm_present(128U))return (uint64_t)-ENODEV;
        if(request==NAS_DRM_IOCTL_VERSION)return drm_version(user_arg,true);
        if(request==NAS_DRM_IOCTL_GET_CAP)return drm_cap(user_arg,true);
        if(request==NAS_DRM_IOCTL_VIRTGPU_EXECBUFFER)
            return render_execbuffer(slot,user_arg);
        if(request==NAS_DRM_IOCTL_VIRTGPU_MAP)
            return render_map(slot,user_arg);
        if(request==NAS_DRM_IOCTL_VIRTGPU_RESOURCE_CREATE)
            return render_resource_create(slot,user_arg);
        if(request==NAS_DRM_IOCTL_VIRTGPU_RESOURCE_INFO)
            return render_resource_info(slot,user_arg);
        if(request==NAS_DRM_IOCTL_GEM_CLOSE)
            return render_gem_close(slot,user_arg);
        if(request==NAS_DRM_IOCTL_VIRTGPU_GETPARAM)
            return render_getparam(user_arg);
        if(request==NAS_DRM_IOCTL_VIRTGPU_GET_CAPS)
            return render_getcaps(user_arg);
        return (uint64_t)-EOPNOTSUPP; /* No pretend GPU rendering. */
    }
#endif
    if(!drm_present(0))return (uint64_t)-ENODEV;
    if(request==NAS_DRM_IOCTL_VERSION)return drm_version(user_arg,false);
    if(request==NAS_DRM_IOCTL_GET_CAP)return drm_cap(user_arg,false);
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
