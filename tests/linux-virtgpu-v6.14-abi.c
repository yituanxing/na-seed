/* Compile-only ABI contract for ORIGINAL Linux v6.14 UAPI definitions.
 * It does not claim kernel virtgpu is linked, nor that Mesa/GL works.
 * This catches packing and ioctl-number regressions across three targets. */
#include <stddef.h>
#include <linux/virtio_gpu.h>
#include <drm/virtgpu_drm.h>

#define CHECK_SIZE(type, bytes) _Static_assert(sizeof(struct type)==(bytes), #type " ABI size")
#define CHECK_OFFSET(type, field, bytes) _Static_assert(offsetof(struct type,field)==(bytes), #type "." #field " offset")

CHECK_SIZE(virtio_gpu_ctrl_hdr,24);
CHECK_OFFSET(virtio_gpu_ctrl_hdr,fence_id,8);
CHECK_OFFSET(virtio_gpu_ctrl_hdr,ctx_id,16);
CHECK_OFFSET(virtio_gpu_ctrl_hdr,ring_idx,20);
CHECK_SIZE(virtio_gpu_resource_create_2d,40);
CHECK_SIZE(virtio_gpu_set_scanout,48);
CHECK_SIZE(virtio_gpu_transfer_to_host_2d,56);
CHECK_SIZE(virtio_gpu_resource_attach_backing,32);
CHECK_SIZE(virtio_gpu_resp_display_info,408);
CHECK_SIZE(virtio_gpu_get_capset_info,32);
CHECK_SIZE(virtio_gpu_resp_capset_info,40);
CHECK_SIZE(virtio_gpu_get_capset,32);
CHECK_SIZE(virtio_gpu_ctx_create,96);
CHECK_SIZE(virtio_gpu_ctx_resource,32);
CHECK_SIZE(virtio_gpu_resource_create_3d,72);
CHECK_SIZE(virtio_gpu_transfer_host_3d,72);
CHECK_SIZE(virtio_gpu_cmd_submit,32);

CHECK_SIZE(drm_virtgpu_getparam,16);
CHECK_SIZE(drm_virtgpu_get_caps,24);
CHECK_SIZE(drm_virtgpu_resource_create,56);
CHECK_SIZE(drm_virtgpu_execbuffer,64);
CHECK_SIZE(drm_virtgpu_3d_wait,8);
CHECK_OFFSET(drm_virtgpu_get_caps,addr,8);
CHECK_OFFSET(drm_virtgpu_execbuffer,command,8);
CHECK_OFFSET(drm_virtgpu_execbuffer,bo_handles,16);
CHECK_OFFSET(drm_virtgpu_execbuffer,in_syncobjs,48);
_Static_assert(DRM_IOCTL_VIRTGPU_GETPARAM == 0xc0106443U,"Linux GETPARAM command");
_Static_assert(DRM_IOCTL_VIRTGPU_GET_CAPS == 0xc0186449U,"Linux GET_CAPS command");
_Static_assert(DRM_IOCTL_VIRTGPU_EXECBUFFER == 0xc0406442U,"Linux EXECBUFFER command");

int nas_linux_virtgpu_uapi_compilation_contract(void) { return 0; }
