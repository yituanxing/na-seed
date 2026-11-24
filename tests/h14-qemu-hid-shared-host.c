/* Exercise the real platform HID helpers through the input core.
 * Only transport completions are scripted; no physical USB proof implied. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/platform/x86_qemu_hid_input.h"
#include "../kernel/drivers/input/evdev.h"
static unsigned step,rejected;
uint64_t nas_arch_monotonic_ns(void) { return 1000000; }
void nas_print(const char *s) { (void)s; ++rejected; }
static int kbd(struct nas_xhci_ro_session *s,uint8_t *r) {
    (void)s;
    if(step++==0){r[2]=4;return 0;}
    if(step==2){r[1]=1;return 0;}
    return -EAGAIN;
}
static int mouse(struct nas_xhci_ro_session *s,uint8_t *r) {
    (void)s;
    if(step++==0){r[0]=1;r[1]=5;r[2]=253;return 0;}
    return -EIO;
}
int main(void) {
    struct nas_input_device k,m,bad;
    struct nas_usb_hid_boot_keyboard ks;
    struct nas_usb_hid_boot_mouse ms;
    nas_usb_hid_boot_init(&ks);
    nas_usb_hid_boot_mouse_init(&ms);
    assert(!nas_x86_qemu_hid_keyboard_register(&k,0));
    assert(k.id.bustype==3 && (k.keybit[30/8]&(1U<<(30%8))));
    assert(nas_x86_qemu_hid_mouse_register(&bad,0)==-EEXIST);
    assert(!bad.registered);
    assert(!nas_x86_qemu_hid_mouse_register(&m,1));
    assert(m.id.bustype==3 && (m.relbit[0]&3)==3);
    uint64_t total=0;
    assert(nas_x86_qemu_hid_keyboard_poll(NULL,kbd,&ks,&k,4,&total,"bad")==2);
    assert(total==2 && rejected==1);
    assert(k.key_state[30/8]&(1U<<(30%8)));
    step=0;
    assert(nas_x86_qemu_hid_mouse_poll(NULL,mouse,&ms,&m,4,&total,"bad")==-EIO);
    assert(total==3 && (m.key_state[272/8]&1U));
    nas_usb_hid_boot_release_all(&ks,nas_x86_qemu_hid_emit,&k);
    nas_usb_hid_boot_mouse_release_all(&ms,nas_x86_qemu_hid_emit,&m);
    assert(!(k.key_state[30/8]&(1U<<(30%8))));
    assert(!(m.key_state[272/8]&1U));
    assert(!nas_input_unregister_device(&m));
    assert(!nas_input_unregister_device(&k));
    puts("[QEMU HID SHARED HOST PASS] registration rollback, capabilities, poll errors and release");
    return 0;
}
