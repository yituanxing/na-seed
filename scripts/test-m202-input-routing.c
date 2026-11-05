/* The real shared input core must keep Linux evdev and virtual-console
 * keyboard dispatch independent. No synthetic UART or QMP stubs involved. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "../kernel/drivers/input/core.h"
#include "../kernel/drivers/input/evdev.h"
#include "../kernel/errno.h"

uint64_t nas_arch_monotonic_ns(void) {
    static uint64_t clock_ns;
    return clock_ns += 1000000U;
}

static struct { uint16_t code; bool pressed; } delivered[8];
static unsigned delivered_count;
static void keyboard_handler(uint16_t code,bool pressed) {
    assert(delivered_count<sizeof(delivered)/sizeof(delivered[0]));
    delivered[delivered_count].code=code;
    delivered[delivered_count].pressed=pressed;
    ++delivered_count;
}

int main(void) {
    assert(nas_input_set_keyboard_handler(NULL)==-EINVAL);
    assert(nas_input_set_keyboard_handler(keyboard_handler)==0);
    assert(nas_input_set_keyboard_handler(keyboard_handler)==-EBUSY);

    struct nas_input_device mouse;
    nas_input_device_init(&mouse,"VirtIO tablet");
    assert(nas_input_set_capability(&mouse,NAS_INPUT_EV_KEY,0x110)==0);
    assert(nas_input_register_device(&mouse)==0);
    nas_input_event(&mouse,NAS_INPUT_EV_KEY,0x110,1);
    nas_input_event(&mouse,NAS_INPUT_EV_SYN,NAS_INPUT_SYN_REPORT,0);
    assert(delivered_count==0);

    struct nas_input_device keyboard;
    nas_input_device_init(&keyboard,"VirtIO keyboard");
    assert(nas_input_set_capability(&keyboard,NAS_INPUT_EV_KEY,30)==0);
    assert(nas_input_set_capability(&keyboard,NAS_INPUT_EV_KEY,16)==0);
    assert(nas_input_register_device(&keyboard)==0);
    struct nas_evdev_client *client=NULL;
    assert(nas_evdev_open(keyboard.index,&client)==0);

    nas_input_event(&keyboard,NAS_INPUT_EV_KEY,16,1);
    nas_input_event(&keyboard,NAS_INPUT_EV_KEY,16,1); /* duplicate suppressed */
    nas_input_event(&keyboard,NAS_INPUT_EV_KEY,16,2); /* Linux key repeat */
    nas_input_event(&keyboard,NAS_INPUT_EV_KEY,16,0);
    nas_input_event(&keyboard,NAS_INPUT_EV_KEY,16,0); /* duplicate suppressed */
    nas_input_event(&keyboard,NAS_INPUT_EV_KEY,17,1); /* not capability */
    nas_input_event(&keyboard,NAS_INPUT_EV_SYN,NAS_INPUT_SYN_REPORT,0);
    assert(delivered_count==3);
    assert(delivered[0].code==16 && delivered[0].pressed);
    assert(delivered[1].code==16 && delivered[1].pressed);
    assert(delivered[2].code==16 && !delivered[2].pressed);

    struct nas_input_event events[5];
    assert(nas_evdev_read(client,events,sizeof(events))==4*24);
    assert(events[0].type==NAS_INPUT_EV_KEY && events[0].value==1);
    assert(events[1].type==NAS_INPUT_EV_KEY && events[1].value==2);
    assert(events[2].type==NAS_INPUT_EV_KEY && events[2].value==0);
    assert(events[3].type==NAS_INPUT_EV_SYN);
    nas_evdev_close(client);
    assert(nas_input_unregister_device(&keyboard)==0);
    assert(nas_input_unregister_device(&mouse)==0);
    puts("M202 keyboard-only N_TTY handler and independent evdev PASS");
    return 0;
}
