/* Host test against real input core and evdev sources, not a fake protocol. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/drivers/input/core.h"
#include "../kernel/drivers/input/evdev.h"
#include "../kernel/errno.h"

static uint64_t clock_ns;
uint64_t nas_arch_monotonic_ns(void) { return clock_ns += 1000000U; }
static unsigned wakes;
static void wakeup(void *unused) {(void)unused; wakes++;}

static void test_keyboard(void) {
    struct nas_input_device keyboard;
    nas_input_device_init(&keyboard, "QEMU Virtio Keyboard");
    assert(nas_input_set_capability(&keyboard,NAS_INPUT_EV_KEY,30)==0);
    assert(nas_input_set_capability(&keyboard,NAS_INPUT_EV_KEY,31)==0);
    assert(nas_input_register_device(&keyboard)==0);
    assert(keyboard.index==0);
    const struct nas_input_device *at=nas_input_device_at(0);
    assert(at==&keyboard && at->evbit&(1U<<NAS_INPUT_EV_KEY));
    size_t capbytes=0;
    const uint8_t *bits=nas_input_capabilities(at,NAS_INPUT_EV_KEY,&capbytes);
    assert(bits && capbytes>=4 && (bits[30/8]&(1U<<(30%8))));

    struct nas_evdev_client *a=NULL,*b=NULL;
    assert(nas_evdev_open(0,&a)==0 && nas_evdev_open(0,&b)==0);
    assert(a!=b);
    nas_evdev_set_wake(a,wakeup,NULL);
    nas_input_event(&keyboard,NAS_INPUT_EV_KEY,30,1);
    nas_input_event(&keyboard,NAS_INPUT_EV_KEY,30,1); /* state duplicate */
    assert(!nas_evdev_ready(a)); /* cannot consume incomplete packet */
    nas_input_event(&keyboard,NAS_INPUT_EV_SYN,NAS_INPUT_SYN_REPORT,0);
    assert(wakes==1);
    struct nas_input_event events[5]={{0}};
    assert(nas_evdev_read(a,events,1)==-EINVAL);
    assert(nas_evdev_read(a,events,sizeof(events))==48);
    assert(events[0].type==NAS_INPUT_EV_KEY && events[0].code==30 &&
           events[0].value==1 && events[0].tv_usec>0);
    assert(events[1].type==NAS_INPUT_EV_SYN &&
           events[1].code==NAS_INPUT_SYN_REPORT);
    assert(!nas_evdev_ready(a));
    assert(nas_evdev_read(a,events,sizeof(events))==-EAGAIN);
    assert(nas_evdev_read(a,events,0)==0);
    assert(nas_evdev_ready(b)); /* separate independent reader */
    assert(nas_evdev_read(b,events,sizeof(events))==48);

    nas_input_event(&keyboard,NAS_INPUT_EV_KEY,30,0);
    nas_input_event(&keyboard,NAS_INPUT_EV_KEY,31,1);
    nas_input_event(&keyboard,NAS_INPUT_EV_SYN,NAS_INPUT_SYN_REPORT,0);
    assert(nas_evdev_read(a,events,sizeof(events))==72);
    assert(events[0].type==NAS_INPUT_EV_KEY && events[0].value==0);
    assert(events[1].type==NAS_INPUT_EV_KEY && events[1].value==1);
    assert(events[2].type==NAS_INPUT_EV_SYN);
    assert(nas_input_unregister_device(&keyboard)==-EBUSY);
    nas_evdev_close(a);
    assert(nas_input_unregister_device(&keyboard)==-EBUSY);
    nas_evdev_close(b);
    assert(nas_input_unregister_device(&keyboard)==0);
    assert(nas_input_device_at(0)==NULL);
}

static void test_mouse_and_overflow(void) {
    struct nas_input_device pointer;
    nas_input_device_init(&pointer,"QEMU Virtio Tablet");
    assert(nas_input_set_absinfo(&pointer,0,0,1023,0,0,0)==0);
    assert(nas_input_set_absinfo(&pointer,1,0,767,0,0,0)==0);
    assert(nas_input_set_capability(&pointer,NAS_INPUT_EV_REL,0)==0);
    assert(nas_input_set_capability(&pointer,NAS_INPUT_EV_KEY,0x110)==0);
    assert(nas_input_register_device(&pointer)==0 && pointer.index==0);
    struct nas_evdev_client *client=NULL;
    assert(nas_evdev_open(0,&client)==0);
    nas_input_event(&pointer,NAS_INPUT_EV_ABS,0,400);
    nas_input_event(&pointer,NAS_INPUT_EV_ABS,1,300);
    nas_input_event(&pointer,NAS_INPUT_EV_KEY,0x110,1);
    nas_input_event(&pointer,NAS_INPUT_EV_SYN,NAS_INPUT_SYN_REPORT,0);
    struct nas_input_event events[8];
    assert(nas_evdev_read(client,events,sizeof(events))==96);
    assert(pointer.absinfo[0].value==400 && pointer.absinfo[1].value==300);
    assert(events[0].type==NAS_INPUT_EV_ABS && events[0].code==0);
    assert(events[2].type==NAS_INPUT_EV_KEY && events[2].code==0x110);

    /* Reader lag must not emit a silently corrupted partial event packet. */
    for(unsigned i=0;i<260U;++i)
        nas_input_event(&pointer,NAS_INPUT_EV_REL,0,1);
    assert(!nas_evdev_ready(client));
    nas_input_event(&pointer,NAS_INPUT_EV_SYN,NAS_INPUT_SYN_REPORT,0);
    assert(nas_evdev_read(client,events,sizeof(events))==48);
    assert(events[0].type==NAS_INPUT_EV_SYN &&
           events[0].code==NAS_INPUT_SYN_DROPPED);
    assert(events[1].type==NAS_INPUT_EV_SYN &&
           events[1].code==NAS_INPUT_SYN_REPORT);
    assert(!nas_evdev_ready(client));
    nas_evdev_close(client);
    assert(nas_input_unregister_device(&pointer)==0);
}

int main(void) {
    test_keyboard();
    test_mouse_and_overflow();
    puts("M166 input core/evdev real provider PASS");
    return 0;
}
