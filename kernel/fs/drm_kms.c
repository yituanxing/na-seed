/* Fixed physical VirtIO 2D scanout exposed through the public Linux DRM
 * legacy KMS ABI. One connector/encoder/CRTC/primary plane describes the
 * existing DisplayDevice; no fake connectors, planes or implicit resolution
 * changes. A KMS framebuffer holds a GEM owner pin independent of handles.
 *
 * Until VirtIO 2D supports per-GEM resource scanout, software KMS presents
 * one fixed-size packed XRGB8888 buffer via the existing DisplayDevice
 * write_at + asynchronous transfer/flush pipeline. This is a real image
 * transfer into QEMU, not an ioctl-success simulation or a second GPU driver.
 */
#include "drm_kms.h"
#include "drm_gem.h"
#include "../drivers/display/display.h"
#include "../drivers/display/virtio_gpu.h"
#include "../arch/arch.h"
#include "../mm/device_backing.h"
#include "../mm/usercopy.h"
#include "../fd.h"
#include "../errno.h"
#include "../lib/string.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define KMS_CRTC 42U
#define KMS_ENCODER 43U
#define KMS_CONNECTOR 44U
#define KMS_PLANE 45U
#define KMS_FB_MAX 32U
#define KMS_XRGB8888 UINT32_C(0x34325258)
#define KMS_INVALID_SLOT NAS_OPEN_FILE_LIMIT
#define KMS_WIDTH 800U
#define KMS_HEIGHT 600U
#define KMS_PITCH 3200U
#define KMS_PAGE 4096U

#define KMS_IOCTL_GETRESOURCES UINT32_C(0xc04064a0)
#define KMS_IOCTL_GETCRTC UINT32_C(0xc06864a1)
#define KMS_IOCTL_SETCRTC UINT32_C(0xc06864a2)
#define KMS_IOCTL_GETENCODER UINT32_C(0xc01464a6)
#define KMS_IOCTL_GETCONNECTOR UINT32_C(0xc05064a7)
#define KMS_IOCTL_GETPLANE_RES UINT32_C(0xc01064b5)
#define KMS_IOCTL_GETPLANE UINT32_C(0xc02064b6)
#define KMS_IOCTL_ADDFB UINT32_C(0xc01c64ae)
#define KMS_IOCTL_ADDFB2 UINT32_C(0xc06864b8)
#define KMS_IOCTL_RMFB UINT32_C(0xc00464af)
#define KMS_IOCTL_DIRTYFB UINT32_C(0xc01864b1)
#define KMS_IOCTL_PAGE_FLIP UINT32_C(0xc01864b0)
#define KMS_PAGE_FLIP_EVENT 1U
#define KMS_EVENT_FLIP_COMPLETE 2U
#define KMS_MAX_EVENTS 16U
#define KMS_IOCTL_SET_CLIENT_CAP UINT32_C(0x4010640d)
#define KMS_IOCTL_OBJ_GETPROPERTIES UINT32_C(0xc02064b9)
#define KMS_IOCTL_GETPROPERTY UINT32_C(0xc04064aa)
#define KMS_PROP_PLANE_TYPE 46U
#define KMS_OBJ_CRTC UINT32_C(0xcccccccc)
#define KMS_OBJ_CONNECTOR UINT32_C(0xc0c0c0c0)
#define KMS_OBJ_PLANE UINT32_C(0xeeeeeeee)
#define KMS_IOCTL_SET_MASTER UINT32_C(0x641e)
#define KMS_IOCTL_DROP_MASTER UINT32_C(0x641f)
struct kms_modeinfo {
    uint32_t clock;
    uint16_t hdisplay,hsync_start,hsync_end,htotal,hskew;
    uint16_t vdisplay,vsync_start,vsync_end,vtotal,vscan;
    uint32_t vrefresh,flags,type;
    char name[32];
};
struct kms_res {
    uint64_t fbs,crtcs,connectors,encoders;
    uint32_t count_fbs,count_crtcs,count_connectors,count_encoders;
    uint32_t min_width,max_width,min_height,max_height;
};
struct kms_crtc {
    uint64_t connectors;
    uint32_t count_connectors,crtc_id,fb_id,x,y,gamma_size,mode_valid;
    struct kms_modeinfo mode;
};
struct kms_connector {
    uint64_t encoders,modes,props,prop_values;
    uint32_t count_modes,count_props,count_encoders;
    uint32_t encoder_id,connector_id,connector_type,connector_type_id;
    uint32_t connection,mm_width,mm_height,subpixel,pad;
};
struct kms_encoder {
    uint32_t encoder_id,encoder_type,crtc_id,possible_crtcs,possible_clones;
};
struct kms_plane_res {uint64_t ids;uint32_t count;};
struct kms_plane {
    uint32_t plane_id,crtc_id,fb_id,possible_crtcs,gamma_size,count_formats;
    uint64_t formats;
};
struct kms_fb {
    uint32_t fb_id,width,height,pitch,bpp,depth,handle;
};
struct kms_fb2 {
    uint32_t fb_id,width,height,format,flags;
    uint32_t handles[4],pitches[4],offsets[4];
    uint64_t modifiers[4];
};
struct kms_dirty {
    uint32_t fb_id,flags,color,count_clips;
    uint64_t clips;
};
/* Linux DRM_MODE_DIRTYFB cliprect UAPI, not a kernel-specific wire format. */
struct kms_clip_rect {uint16_t x1,y1,x2,y2;};
_Static_assert(sizeof(struct kms_clip_rect)==8U,"Linux DRM cliprect");
struct kms_client_cap {uint64_t capability,value;};
struct kms_page_flip {
    uint32_t crtc_id,fb_id,flags,reserved;
    uint64_t user_data;
};
struct kms_flip_event {
    uint32_t type,length;
    uint64_t user_data;
    uint32_t tv_sec,tv_usec,sequence,crtc_id;
};
_Static_assert(sizeof(struct kms_page_flip)==24U,"DRM page flip");
_Static_assert(sizeof(struct kms_flip_event)==32U,"DRM event_vblank");
struct kms_obj_properties {
    uint64_t props,values;
    uint32_t count,obj_id,obj_type;
};
struct kms_get_property {
    uint64_t values,enums;
    uint32_t id,flags;
    char name[32];
    uint32_t count_values,count_enums;
};
struct kms_property_enum {uint64_t value;char name[32];};
_Static_assert(sizeof(struct kms_obj_properties)==32U,"DRM object props");
_Static_assert(sizeof(struct kms_get_property)==64U,"DRM get property");
_Static_assert(sizeof(struct kms_property_enum)==40U,"DRM enum property");
_Static_assert(sizeof(struct kms_modeinfo)==68U,"Linux DRM modeinfo");
_Static_assert(sizeof(struct kms_res)==64U,"Linux DRM resources");
_Static_assert(sizeof(struct kms_crtc)==104U,"Linux DRM CRTC");
_Static_assert(sizeof(struct kms_connector)==80U,"Linux DRM connector");
_Static_assert(sizeof(struct kms_encoder)==20U,"Linux DRM encoder");
_Static_assert(sizeof(struct kms_plane_res)==16U,"Linux DRM plane resources");
_Static_assert(sizeof(struct kms_plane)==32U,"Linux DRM plane");
_Static_assert(sizeof(struct kms_fb)==28U,"Linux DRM addfb");
_Static_assert(sizeof(struct kms_fb2)==104U,"Linux DRM addfb2");
_Static_assert(sizeof(struct kms_dirty)==24U,"Linux DRM dirtyfb");
_Static_assert(sizeof(struct kms_client_cap)==16U,"Linux DRM client cap");

struct kms_framebuffer {
    struct nas_vm_device *backing;
    uint32_t id,pitch,owner_slot;
};
static struct kms_framebuffer fbs[KMS_FB_MAX];
static uint32_t next_fb_id=64U;
static unsigned master_slot=KMS_INVALID_SLOT;
static uint32_t current_fb;
static bool crtc_enabled,universal_planes[NAS_OPEN_FILE_LIMIT];
/* Linux event records belong to a shared OpenFile, so dup/fork receive the
 * same queue. The scheduler owns completion; usercopy happens only in read. */
struct kms_event_fifo {
    struct kms_flip_event records[KMS_MAX_EVENTS];
    uint8_t read_index,count;
};
static struct kms_event_fifo event_fifo[NAS_OPEN_FILE_LIMIT];
static struct {
    bool pending,staged,want_event;
    unsigned owner_slot;
    uint32_t fb_id;
    uint64_t user_data,submitted_serial;
} flip;
static uint32_t flip_sequence;

static bool kms_available(void) {
    const struct nas_display_device *d=nas_display_primary();
    return d && d->mode.width==KMS_WIDTH &&
           d->mode.height==KMS_HEIGHT && d->mode.pitch==KMS_PITCH &&
           d->mode.bpp==32U && d->ops && d->ops->write_at &&
           d->ops->present;
}
static struct kms_modeinfo fixed_mode(void) {
    struct kms_modeinfo mode={0};
    mode.clock=40000;mode.hdisplay=800;mode.hsync_start=840;
    mode.hsync_end=968;mode.htotal=1056;mode.vdisplay=600;
    mode.vsync_start=601;mode.vsync_end=605;mode.vtotal=628;
    mode.vrefresh=60;mode.type=(1U<<3)|(1U<<6);
    memcpy(mode.name,"800x600",8);
    return mode;
}
static bool valid_mode(const struct kms_modeinfo *mode) {
    struct kms_modeinfo ours=fixed_mode();
    return mode && mode->hdisplay==ours.hdisplay &&
           mode->vdisplay==ours.vdisplay && mode->clock==ours.clock &&
           mode->hsync_start==ours.hsync_start &&
           mode->hsync_end==ours.hsync_end &&
           mode->htotal==ours.htotal && mode->vsync_start==ours.vsync_start &&
           mode->vsync_end==ours.vsync_end && mode->vtotal==ours.vtotal &&
           mode->vscan==ours.vscan;
}
static struct kms_framebuffer *find_fb(uint32_t id) {
    if(!id)return NULL;
    for(unsigned i=0;i<KMS_FB_MAX;i++)
        if(fbs[i].backing && fbs[i].id==id)return &fbs[i];
    return NULL;
}
static int copy_u32s(uint64_t dest,uint32_t capacity,
                     const uint32_t *src,uint32_t count) {
    if(!dest || capacity<count || !count)return 0;
    return nas_user_write_bytes(dest,(const uint8_t *)src,
                                (uint64_t)count*sizeof(uint32_t))?0:-EFAULT;
}
static int copyout(uint64_t dest,const void *src,size_t count) {
    return nas_user_write_bytes(dest,(const uint8_t *)src,count)?0:-EFAULT;
}
static int copyin(void *dest,uint64_t src,size_t count) {
    return nas_user_read_bytes(src,(uint8_t *)dest,count)?0:-EFAULT;
}
static int scanout_copy(struct kms_framebuffer *fb) {
    if(!kms_available() || !fb || !fb->backing)return -ENODEV;
    uint8_t scratch[KMS_PAGE];
    for(uint64_t pos=0;pos<(uint64_t)KMS_PITCH*KMS_HEIGHT;) {
        uint64_t bytes=(uint64_t)KMS_PITCH*KMS_HEIGHT-pos;
        if(bytes>KMS_PAGE)bytes=KMS_PAGE;
        int rc=nas_drm_gem_copy_from(fb->backing,pos,scratch,bytes);
        if(rc)return rc;
        if(nas_display_write_at(pos,scratch,bytes)!=bytes)return -EIO;
        pos+=bytes;
    }
    return nas_display_present();
}
static uint64_t resources(unsigned slot,uint64_t arg) {
    struct kms_res r;int rc=copyin(&r,arg,sizeof(r));if(rc)return (uint64_t)(int64_t)rc;
    uint32_t fb_ids[KMS_FB_MAX];unsigned n=0;
    for(unsigned i=0;i<KMS_FB_MAX;i++)
        if(fbs[i].backing && fbs[i].owner_slot==slot)fb_ids[n++]=fbs[i].id;
    const uint32_t crtc=KMS_CRTC,encoder=KMS_ENCODER,conn=KMS_CONNECTOR;
    if((rc=copy_u32s(r.fbs,r.count_fbs,fb_ids,n)) ||
       (rc=copy_u32s(r.crtcs,r.count_crtcs,&crtc,1)) ||
       (rc=copy_u32s(r.connectors,r.count_connectors,&conn,1)) ||
       (rc=copy_u32s(r.encoders,r.count_encoders,&encoder,1)))
        return (uint64_t)(int64_t)rc;
    r.count_fbs=n;r.count_crtcs=1;r.count_connectors=1;r.count_encoders=1;
    /* This fixed-mode KMS accepts only KMS_WIDTH x KMS_HEIGHT in ADDFB.
     * Report the actual framebuffer limits: Xorg verifies the 32-bpp
     * path using the minimum advertised size before initializing X11. */
    r.min_width=KMS_WIDTH;r.min_height=KMS_HEIGHT;
    r.max_width=KMS_WIDTH;r.max_height=KMS_HEIGHT;
    return (uint64_t)(int64_t)copyout(arg,&r,sizeof(r));
}
static uint64_t get_connector(uint64_t arg) {
    struct kms_connector c;int rc=copyin(&c,arg,sizeof(c));
    if(rc)return (uint64_t)(int64_t)rc;
    if(c.connector_id!=KMS_CONNECTOR)return (uint64_t)-ENOENT;
    const uint32_t encoder=KMS_ENCODER;
    const struct kms_modeinfo m=fixed_mode();
    if((rc=copy_u32s(c.encoders,c.count_encoders,&encoder,1)))
        return (uint64_t)(int64_t)rc;
    if(c.modes && c.count_modes>=1U) {
        rc=copyout(c.modes,&m,sizeof(m));
        if(rc)return (uint64_t)(int64_t)rc;
    }
    c.count_modes=1;c.count_props=0;c.count_encoders=1;
    c.encoder_id=KMS_ENCODER;c.connector_type=15;c.connector_type_id=1;
    c.connection=1;c.mm_width=0;c.mm_height=0;c.subpixel=0;c.pad=0;
    return (uint64_t)(int64_t)copyout(arg,&c,sizeof(c));
}
static uint64_t get_encoder(uint64_t arg) {
    struct kms_encoder e;int rc=copyin(&e,arg,sizeof(e));
    if(rc)return (uint64_t)(int64_t)rc;
    if(e.encoder_id!=KMS_ENCODER)return (uint64_t)-ENOENT;
    e.encoder_type=5;e.crtc_id=KMS_CRTC;e.possible_crtcs=1;e.possible_clones=0;
    return (uint64_t)(int64_t)copyout(arg,&e,sizeof(e));
}
static uint64_t get_crtc(uint64_t arg) {
    struct kms_crtc c;int rc=copyin(&c,arg,sizeof(c));
    if(rc)return (uint64_t)(int64_t)rc;
    if(c.crtc_id!=KMS_CRTC)return (uint64_t)-ENOENT;
    c.connectors=0;c.count_connectors=0;c.fb_id=crtc_enabled?current_fb:0;
    c.x=0;c.y=0;c.gamma_size=0;c.mode_valid=crtc_enabled?1U:0U;
    c.mode=crtc_enabled?fixed_mode():(struct kms_modeinfo){0};
    return (uint64_t)(int64_t)copyout(arg,&c,sizeof(c));
}
static uint64_t plane_resources(unsigned slot,uint64_t arg) {
    struct kms_plane_res r;int rc=copyin(&r,arg,sizeof(r));
    if(rc)return (uint64_t)(int64_t)rc;
    const uint32_t plane=KMS_PLANE;
    /* Linux hides primary planes without UNIVERSAL_PLANES capability. */
    unsigned n=universal_planes[slot]?1U:0U;
    if((rc=copy_u32s(r.ids,r.count,&plane,n)))return (uint64_t)(int64_t)rc;
    r.count=n;
    return (uint64_t)(int64_t)copyout(arg,&r,sizeof(r));
}
static uint64_t get_plane(uint64_t arg) {
    struct kms_plane p;int rc=copyin(&p,arg,sizeof(p));
    if(rc)return (uint64_t)(int64_t)rc;
    if(p.plane_id!=KMS_PLANE)return (uint64_t)-ENOENT;
    const uint32_t format=KMS_XRGB8888;
    if((rc=copy_u32s(p.formats,p.count_formats,&format,1)))
        return (uint64_t)(int64_t)rc;
    p.crtc_id=crtc_enabled?KMS_CRTC:0;p.fb_id=crtc_enabled?current_fb:0;
    p.possible_crtcs=1;p.gamma_size=0;p.count_formats=1;
    return (uint64_t)(int64_t)copyout(arg,&p,sizeof(p));
}
/* DRM objects are discoverable without invented properties. A primary
 * plane genuinely carries the standard immutable "type" enum. The legacy
 * KMS API does not require atomic-only FB_ID/CRTC_ID property controls. */
static uint64_t object_properties(uint64_t arg) {
    struct kms_obj_properties req;
    int rc=copyin(&req,arg,sizeof(req));
    if(rc)return (uint64_t)(int64_t)rc;
    const uint32_t plane_property=KMS_PROP_PLANE_TYPE;
    const uint64_t plane_type=1U; /* DRM_PLANE_TYPE_PRIMARY */
    uint32_t count=0;
    if(req.obj_id==KMS_CRTC && req.obj_type==KMS_OBJ_CRTC)
        count=0;
    else if(req.obj_id==KMS_CONNECTOR &&
            req.obj_type==KMS_OBJ_CONNECTOR)count=0;
    else if(req.obj_id==KMS_PLANE && req.obj_type==KMS_OBJ_PLANE)
        count=1;
    else return (uint64_t)-ENOENT;
    if(count && req.count>=count && req.props && req.values) {
        rc=copyout(req.props,&plane_property,sizeof(plane_property));
        if(!rc)rc=copyout(req.values,&plane_type,sizeof(plane_type));
        if(rc)return (uint64_t)(int64_t)rc;
    }
    req.count=count;
    return (uint64_t)(int64_t)copyout(arg,&req,sizeof(req));
}
static uint64_t get_property(uint64_t arg) {
    struct kms_get_property prop;
    int rc=copyin(&prop,arg,sizeof(prop));
    if(rc)return (uint64_t)(int64_t)rc;
    if(prop.id!=KMS_PROP_PLANE_TYPE)return (uint64_t)-ENOENT;
    static const struct kms_property_enum enums[3]={
        {0,"Overlay"},{1,"Primary"},{2,"Cursor"}
    };
    if(prop.enums && prop.count_enums>=3U) {
        rc=copyout(prop.enums,enums,sizeof(enums));
        if(rc)return (uint64_t)(int64_t)rc;
    }
    prop.flags=(1U<<2)|(1U<<3); /* IMMUTABLE | ENUM */
    memset(prop.name,0,sizeof(prop.name));
    memcpy(prop.name,"type",5U);
    prop.count_values=0;
    prop.count_enums=3U;
    return (uint64_t)(int64_t)copyout(arg,&prop,sizeof(prop));
}
static uint64_t add_fb(unsigned slot,uint64_t arg,bool modern) {
    struct nas_vm_device *backing=NULL;
    uint32_t handle=0,pitch=0,width=0,height=0;
    int rc=0;
    struct kms_fb2 fb2={0};struct kms_fb legacy={0};
    if(modern) {
        if((rc=copyin(&fb2,arg,sizeof(fb2))))return (uint64_t)(int64_t)rc;
        if(fb2.flags || fb2.format!=KMS_XRGB8888)return (uint64_t)-EINVAL;
        for(unsigned i=1;i<4;i++)
            if(fb2.handles[i] || fb2.pitches[i] || fb2.offsets[i] ||
               fb2.modifiers[i])return (uint64_t)-EINVAL;
        if(fb2.offsets[0] || fb2.modifiers[0])return (uint64_t)-EINVAL;
        handle=fb2.handles[0];pitch=fb2.pitches[0];
        width=fb2.width;height=fb2.height;
    } else {
        if((rc=copyin(&legacy,arg,sizeof(legacy))))
            return (uint64_t)(int64_t)rc;
        if(legacy.bpp!=32U || legacy.depth!=24U)return (uint64_t)-EINVAL;
        handle=legacy.handle;pitch=legacy.pitch;
        width=legacy.width;height=legacy.height;
    }
    if(width!=KMS_WIDTH || height!=KMS_HEIGHT || pitch!=KMS_PITCH)
        return (uint64_t)-EINVAL;
    uint64_t bytes=0;uint32_t true_pitch=0;
    rc=nas_drm_gem_pin_fb(slot,handle,&backing,&bytes,&true_pitch);
    if(rc)return (uint64_t)(int64_t)rc;
    if(true_pitch!=pitch || bytes<(uint64_t)KMS_PITCH*KMS_HEIGHT) {
        backing->put(backing->context);
        return (uint64_t)-EINVAL;
    }
    struct kms_framebuffer *target=NULL;
    for(unsigned i=0;i<KMS_FB_MAX;i++)
        if(!fbs[i].backing){target=&fbs[i];break;}
    if(!target || next_fb_id==UINT32_MAX) {
        backing->put(backing->context);
        return (uint64_t)-ENOSPC;
    }
    if(modern)fb2.fb_id=next_fb_id;
    else legacy.fb_id=next_fb_id;
    rc=copyout(arg,modern?(const void *)&fb2:(const void *)&legacy,
               modern?sizeof(fb2):sizeof(legacy));
    if(rc) {
        backing->put(backing->context);
        return (uint64_t)(int64_t)rc;
    }
    *target=(struct kms_framebuffer){
        .backing=backing,.id=next_fb_id++,
        .pitch=pitch,.owner_slot=slot
    };
    return 0;
}
static int release_fb(struct kms_framebuffer *fb) {
    if(!fb || !fb->backing)return -ENOENT;
    if(current_fb==fb->id) {current_fb=0;crtc_enabled=false;}
    struct nas_vm_device *backing=fb->backing;
    *fb=(struct kms_framebuffer){0};
    backing->put(backing->context);
    return 0;
}
/* Do not reclaim the backing while an accepted flip still needs it. */
static uint64_t rm_fb(unsigned slot,uint64_t arg) {
    uint32_t id=0;int rc=copyin(&id,arg,sizeof(id));
    if(rc)return (uint64_t)(int64_t)rc;
    struct kms_framebuffer *fb=find_fb(id);
    if(!fb)return (uint64_t)-ENOENT;
    if(fb->owner_slot!=slot)return (uint64_t)-EACCES;
    if(flip.pending && flip.fb_id==fb->id)return (uint64_t)-EBUSY;
    return (uint64_t)(int64_t)release_fb(fb);
}
static uint64_t set_crtc(unsigned slot,uint64_t arg) {
    struct kms_crtc c;int rc=copyin(&c,arg,sizeof(c));
    if(rc)return (uint64_t)(int64_t)rc;
    if(c.crtc_id!=KMS_CRTC)return (uint64_t)-ENOENT;
    if(master_slot!=slot)return (uint64_t)-EACCES;
    if(flip.pending)return (uint64_t)-EBUSY;
    if(!c.fb_id) {
        if(c.count_connectors || c.mode_valid)return (uint64_t)-EINVAL;
        current_fb=0;crtc_enabled=false;return 0;
    }
    if(c.x || c.y || c.count_connectors!=1U ||
       !c.connectors || !c.mode_valid || !valid_mode(&c.mode))
        return (uint64_t)-EINVAL;
    uint32_t connector=0;rc=copyin(&connector,c.connectors,sizeof(connector));
    if(rc)return (uint64_t)(int64_t)rc;
    if(connector!=KMS_CONNECTOR)return (uint64_t)-EINVAL;
    struct kms_framebuffer *fb=find_fb(c.fb_id);
    if(!fb || fb->owner_slot!=slot)return (uint64_t)-ENOENT;
    rc=scanout_copy(fb);
    if(rc)return (uint64_t)(int64_t)rc;
    current_fb=fb->id;crtc_enabled=true;
    return 0;
}
static uint64_t dirty_fb(unsigned slot,uint64_t arg) {
    struct kms_dirty cmd;int rc=copyin(&cmd,arg,sizeof(cmd));
    if(rc)return (uint64_t)(int64_t)rc;
    /* Linux DRM_MODE_DIRTYFB delivers Xorg's actual damage cliprects.
     * The underlying VirtIO 2D staging path currently uploads a complete
     * XRGB8888 buffer; that is a correct conservative superset of any clip.
     * Validate the user pointers and ranges before committing a real frame. */
    if(cmd.flags)return (uint64_t)-EOPNOTSUPP;
    if(cmd.count_clips) {
        if(!cmd.clips || cmd.count_clips>65536U ||
           cmd.clips>UINT64_MAX-(uint64_t)cmd.count_clips*8U)
            return (uint64_t)-EINVAL;
        for(uint32_t i=0;i<cmd.count_clips;i++) {
            struct kms_clip_rect clip;
            rc=copyin(&clip,cmd.clips+(uint64_t)i*sizeof(clip),
                      sizeof(clip));
            if(rc)return (uint64_t)(int64_t)rc;
            if(clip.x1>clip.x2 || clip.y1>clip.y2 ||
               clip.x2>KMS_WIDTH || clip.y2>KMS_HEIGHT)
                return (uint64_t)-EINVAL;
        }
    }
    struct kms_framebuffer *fb=find_fb(cmd.fb_id);
    if(!fb || fb->owner_slot!=slot)return (uint64_t)-ENOENT;
    if(!crtc_enabled || current_fb!=fb->id)return 0;
    return (uint64_t)(int64_t)scanout_copy(fb);
}
static uint64_t page_flip(unsigned slot,uint64_t arg) {
    struct kms_page_flip req;
    int rc=copyin(&req,arg,sizeof(req));
    if(rc)return (uint64_t)(int64_t)rc;
    if(req.crtc_id!=KMS_CRTC)return (uint64_t)-ENOENT;
    if(req.flags&~KMS_PAGE_FLIP_EVENT || req.reserved)return (uint64_t)-EINVAL;
    if(master_slot!=slot)return (uint64_t)-EACCES;
    if(!crtc_enabled || !current_fb)return (uint64_t)-EINVAL;
    if(flip.pending)return (uint64_t)-EBUSY;
    struct kms_framebuffer *fb=find_fb(req.fb_id);
    if(!fb || fb->owner_slot!=slot)return (uint64_t)-ENOENT;
    if((req.flags&KMS_PAGE_FLIP_EVENT) &&
       event_fifo[slot].count>=KMS_MAX_EVENTS)return (uint64_t)-EBUSY;
    /* Accept only one outstanding flip on the single real CRTC. The
     * staging copy and GPU submit are deferred until previous flushes
     * retire, never performed against an in-flight DMA transfer. */
    flip.pending=true;
    flip.staged=false;
    flip.want_event=(req.flags&KMS_PAGE_FLIP_EVENT)!=0;
    flip.owner_slot=slot;
    flip.fb_id=req.fb_id;
    flip.user_data=req.user_data;
    flip.submitted_serial=0;
    return 0;
}
static void emit_flip_event(unsigned slot,uint64_t user_data) {
    if(slot>=NAS_OPEN_FILE_LIMIT)return;
    struct kms_event_fifo *fifo=&event_fifo[slot];
    if(fifo->count>=KMS_MAX_EVENTS)__builtin_trap();
    uint64_t ns=nas_arch_monotonic_ns();
    struct kms_flip_event event={
        .type=KMS_EVENT_FLIP_COMPLETE,
        .length=sizeof(struct kms_flip_event),
        .user_data=user_data,
        .tv_sec=(uint32_t)(ns/UINT64_C(1000000000)),
        .tv_usec=(uint32_t)((ns%UINT64_C(1000000000))/1000U),
        .sequence=++flip_sequence,.crtc_id=KMS_CRTC
    };
    unsigned tail=(fifo->read_index+fifo->count)%KMS_MAX_EVENTS;
    fifo->records[tail]=event;
    ++fifo->count;
}
int nas_drm_kms_deferred_step(void) {
    if(!flip.pending)return 0;
    struct nas_virtio_gpu_scanout_state state={0};
    nas_virtio_gpu_get_scanout_state(&state);
    if(!state.ready)return -ENODEV;
    if(!flip.staged) {
        if(state.busy || state.dirty)return 0;
        struct kms_framebuffer *fb=find_fb(flip.fb_id);
        if(!fb || fb->owner_slot!=flip.owner_slot)return -EIO;
        /* This path submits a real 800x600 VirtIO resource transfer and
         * resource flush through the already owned DisplayDevice. */
        int rc=scanout_copy(fb);
        if(rc)return rc;
        nas_virtio_gpu_get_scanout_state(&state);
        flip.submitted_serial=state.dirty_serial;
        flip.staged=true;
        return 0;
    }
    if(state.displayed_serial<flip.submitted_serial)return 0;
    /* Completion is after the host acknowledges the final flush, not
     * after the PAGE_FLIP ioctl or CPU-side GEM copy. */
    current_fb=flip.fb_id;
    crtc_enabled=true;
    unsigned owner=flip.owner_slot;
    uint64_t data=flip.user_data;
    bool event=flip.want_event;
    flip.pending=false;
    flip.staged=false;
    if(event)emit_flip_event(owner,data);
    return 0;
}
uint64_t nas_drm_kms_read_plan(unsigned slot,uint64_t requested) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !nas_file_slot_live(slot))
        return (uint64_t)-EBADF;
    if(!requested)return 0;
    if(requested<sizeof(struct kms_flip_event))
        return (uint64_t)-EINVAL;
    const struct kms_event_fifo *fifo=&event_fifo[slot];
    if(!fifo->count)return (uint64_t)-EAGAIN;
    uint64_t records=requested/sizeof(struct kms_flip_event);
    if(records>fifo->count)records=fifo->count;
    return records*sizeof(struct kms_flip_event);
}
uint64_t nas_drm_kms_read(unsigned slot,uint8_t *dst,uint64_t bytes) {
    uint64_t budget=nas_drm_kms_read_plan(slot,bytes);
    if((int64_t)budget<0)return budget;
    if(bytes<budget || !dst)return (uint64_t)-EFAULT;
    struct kms_event_fifo *fifo=&event_fifo[slot];
    for(uint64_t i=0;i<budget/sizeof(struct kms_flip_event);i++) {
        memcpy(dst+i*sizeof(struct kms_flip_event),
               &fifo->records[fifo->read_index],
               sizeof(struct kms_flip_event));
        fifo->read_index=(fifo->read_index+1U)%KMS_MAX_EVENTS;
        --fifo->count;
    }
    return budget;
}
uint16_t nas_drm_kms_poll(unsigned slot,uint16_t requested) {
    return slot<NAS_OPEN_FILE_LIMIT && (requested&1U) &&
           event_fifo[slot].count ? 1U:0U;
}
int nas_drm_kms_open(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT)return -EINVAL;
    universal_planes[slot]=false;
    event_fifo[slot]=(struct kms_event_fifo){0};
    if(master_slot==KMS_INVALID_SLOT)master_slot=slot;
    return 0;
}
void nas_drm_kms_release(unsigned slot) {
    if(slot>=NAS_OPEN_FILE_LIMIT)return;
    if(flip.pending && flip.owner_slot==slot)flip.pending=false;
    event_fifo[slot]=(struct kms_event_fifo){0};
    for(unsigned i=0;i<KMS_FB_MAX;i++)
        if(fbs[i].backing && fbs[i].owner_slot==slot)
            (void)release_fb(&fbs[i]);
    if(master_slot==slot)master_slot=KMS_INVALID_SLOT;
    universal_planes[slot]=false;
}
uint64_t nas_drm_kms_ioctl(unsigned slot,uint32_t cmd,uint64_t arg) {
    if(!kms_available())return (uint64_t)-ENODEV;
    if(slot>=NAS_OPEN_FILE_LIMIT)return (uint64_t)-EBADF;
    if(cmd==KMS_IOCTL_SET_MASTER) {
        if(master_slot!=KMS_INVALID_SLOT && master_slot!=slot)
            return (uint64_t)-EBUSY;
        master_slot=slot;return 0;
    }
    if(cmd==KMS_IOCTL_DROP_MASTER) {
        if(master_slot!=slot)return (uint64_t)-EINVAL;
        if(flip.pending && flip.owner_slot==slot)return (uint64_t)-EBUSY;
        master_slot=KMS_INVALID_SLOT;return 0;
    }
    if(cmd==KMS_IOCTL_SET_CLIENT_CAP) {
        struct kms_client_cap cap;
        int rc=copyin(&cap,arg,sizeof(cap));
        if(rc)return (uint64_t)(int64_t)rc;
        if(cap.capability!=2U || cap.value>1U)
            return (uint64_t)-EINVAL;
        universal_planes[slot]=cap.value!=0;
        return 0;
    }
    switch(cmd) {
    case KMS_IOCTL_GETRESOURCES:return resources(slot,arg);
    case KMS_IOCTL_GETCRTC:return get_crtc(arg);
    case KMS_IOCTL_SETCRTC:return set_crtc(slot,arg);
    case KMS_IOCTL_GETCONNECTOR:return get_connector(arg);
    case KMS_IOCTL_GETENCODER:return get_encoder(arg);
    case KMS_IOCTL_GETPLANE_RES:return plane_resources(slot,arg);
    case KMS_IOCTL_GETPLANE:return get_plane(arg);
    case KMS_IOCTL_ADDFB:return add_fb(slot,arg,false);
    case KMS_IOCTL_ADDFB2:return add_fb(slot,arg,true);
    case KMS_IOCTL_RMFB:return rm_fb(slot,arg);
    case KMS_IOCTL_DIRTYFB:return dirty_fb(slot,arg);
    case KMS_IOCTL_PAGE_FLIP:return page_flip(slot,arg);
    case KMS_IOCTL_OBJ_GETPROPERTIES:return object_properties(arg);
    case KMS_IOCTL_GETPROPERTY:return get_property(arg);
    default:return (uint64_t)-ENOTTY;
    }
}
