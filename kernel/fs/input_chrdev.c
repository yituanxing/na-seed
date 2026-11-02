/* Linux evdev character endpoint. Linux wire encoding and usercopy belong
 * here; Linux-style input core and VirtIO hardware never see user pointers. */
#include "input_chrdev.h"
#include "../drivers/core/chrdev.h"
#include "../drivers/input/core.h"
#include "../drivers/input/evdev.h"
#include "../fd.h"
#include "../task.h"
#include "../errno.h"
#include "../mm/usercopy.h"
#include "../lib/string.h"
#include <stddef.h>
#include <stdint.h>

static struct nas_evdev_client *clients[NAS_OPEN_FILE_LIMIT];

static int describe(const char *path,struct nas_chrdev_info *out) {
    static const char prefix[]="/dev/input/event";
    if(!path || !out || strncmp(path,prefix,sizeof(prefix)-1U))return -ENOENT;
    const char *digits=path+sizeof(prefix)-1U;
    if(!*digits || (*digits=='0' && digits[1]))return -ENOENT;
    unsigned index=0;
    for(;*digits;digits++) {
        if(*digits<'0' || *digits>'9')return -ENOENT;
        index=index*10U+(unsigned)(*digits-'0');
        if(index>=NAS_INPUT_MAX_DEVICES)return -ENOENT;
    }
    if(!nas_input_device_at(index))return -ENOENT;
    *out=(struct nas_chrdev_info){
        .fd_kind=FD_EVDEV,.unit=index,.dev=2,
        .ino=3000U+index,.rdev=(13U<<8)|(64U+index),
        .mode=0020600U
    };
    return 0;
}
static int open_slot(unsigned slot,unsigned unit) {
    if(slot>=NAS_OPEN_FILE_LIMIT)return -EINVAL;
    if(clients[slot])return -EBUSY;
    return nas_evdev_open(unit,&clients[slot]);
}
static const struct nas_chrdev_provider evdev_provider={
    .name="linux-evdev",.describe=describe,.open=open_slot
};
int nas_input_chrdev_init(void) {
    return nas_chrdev_provider_register(&evdev_provider);
}
uint64_t nas_input_chrdev_read_plan(unsigned slot,uint64_t requested) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !clients[slot])return (uint64_t)-ENODEV;
    if(!requested)return 0;
    if(requested<sizeof(struct nas_input_event))return (uint64_t)-EINVAL;
    if(!nas_evdev_ready(clients[slot])) {
        /* Generic asynchronous File wait parks on EOPNOTSUPP, while an
         * O_NONBLOCK caller must observe Linux's EAGAIN. */
        return (uint64_t)((nas_open_files[slot].status_flags&0x800ULL) ?
                          -EAGAIN : -EOPNOTSUPP);
    }
    return requested-(requested%sizeof(struct nas_input_event));
}
uint64_t nas_input_chrdev_read(unsigned slot,uint8_t *dst,uint64_t n) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !clients[slot])return (uint64_t)-ENODEV;
    return (uint64_t)(int64_t)nas_evdev_read(clients[slot],dst,(size_t)n);
}
uint16_t nas_input_chrdev_poll(unsigned slot,uint16_t events) {
    if(slot>=NAS_OPEN_FILE_LIMIT || !clients[slot])return 0x0018U;
    return nas_evdev_ready(clients[slot]) ? (events&0x0001U) : 0;
}
void nas_input_chrdev_release(unsigned slot) {
    if(slot<NAS_OPEN_FILE_LIMIT && clients[slot]) {
        nas_evdev_close(clients[slot]);
        clients[slot]=NULL;
    }
}
static uint64_t copy_result(uint64_t address,const void *value,size_t bytes) {
    return nas_user_write_bytes(address,value,bytes)?0:(uint64_t)-EFAULT;
}
static uint64_t copy_bitmap(uint64_t address,const uint8_t *bitmap,
                            size_t available,size_t requested) {
    if(!requested)return 0;
    if(!bitmap)return (uint64_t)-EINVAL;
    size_t n=available<requested?available:requested;
    return nas_user_write_bytes(address,bitmap,n) ? n : (uint64_t)-EFAULT;
}
uint64_t nas_input_chrdev_ioctl(uint64_t fd,uint64_t cmd,uint64_t arg) {
    if(fd>=NAS_FD_LIMIT || nas_current_task->fds->entries[fd].kind!=FD_EVDEV)
        return (uint64_t)-EBADF;
    unsigned slot=nas_current_task->fds->entries[fd].slot;
    if(slot>=NAS_OPEN_FILE_LIMIT || !clients[slot])return (uint64_t)-ENODEV;
    const struct nas_input_device *d=nas_evdev_device(clients[slot]);
    if(!d)return (uint64_t)-ENODEV;
    uint32_t request=(uint32_t)cmd;
    /* Linux ioctl _IOC(dir,size,'E',nr). The rest of the ioctl adapter
     * retains its normal FIONBIO/CLOEXEC handling and File lifecycle. */
    if(((request>>8)&255U)!='E')return (uint64_t)-ENOTTY;
    if((request>>30)!=2U)return (uint64_t)-ENOTTY; /* _IOC_READ */
    unsigned nr=request&255U;
    size_t bytes=(request>>16)&0x3fffU;
    if(nr==0x01U && bytes>=4U) { /* EVIOCGVERSION */
        const uint32_t version=0x010001U;
        return copy_result(arg,&version,sizeof(version));
    }
    if(nr==0x02U && bytes>=sizeof(d->id)) /* EVIOCGID */
        return copy_result(arg,&d->id,sizeof(d->id));
    if(nr==0x06U) { /* EVIOCGNAME(len), including terminating NUL */
        size_t actual=strnlen(d->name,sizeof(d->name));
        if(actual==sizeof(d->name))return (uint64_t)-EIO;
        actual++;
        if(!bytes)return 0;
        size_t n=actual<bytes?actual:bytes;
        if(!nas_user_write_bytes(arg,(const uint8_t *)d->name,n))
            return (uint64_t)-EFAULT;
        return n;
    }
    if(nr==0x09U) /* EVIOCGPROP(len) */
        return copy_bitmap(arg,d->propbit,sizeof(d->propbit),bytes);
    if(nr==0x18U) /* EVIOCGKEY(len), state on live device */
        return copy_bitmap(arg,d->key_state,sizeof(d->key_state),bytes);
    if(nr>=0x20U && nr<0x20U+32U) { /* EVIOCGBIT(ev,len) */
        size_t cap=0;
        const uint8_t *bits=nas_input_capabilities(d,(uint16_t)(nr-0x20U),&cap);
        if(!bits)return (uint64_t)-EINVAL;
        return copy_bitmap(arg,bits,cap,bytes);
    }
    if(nr>=0x40U && nr<=0x40U+NAS_INPUT_ABS_MAX &&
       bytes>=sizeof(struct nas_input_absinfo)) { /* EVIOCGABS */
        const unsigned axis=nr-0x40U;
        if(!(d->absbit[axis/8U] & (1U<<(axis%8U))))
            return (uint64_t)-EINVAL;
        return copy_result(arg,&d->absinfo[axis],sizeof(d->absinfo[axis]));
    }
    return (uint64_t)-ENOTTY;
}
