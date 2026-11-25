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
    struct nas_x86_qemu_hid_device k={0},m={0},bad={0};
    assert(!nas_x86_qemu_hid_publish(&k,0,false));
    assert(k.input.id.bustype==3 && (k.input.keybit[30/8]&(1U<<(30%8))));
    assert(nas_x86_qemu_hid_publish(&bad,0,true)==-EEXIST);
    assert(!bad.input.registered);
    assert(!nas_x86_qemu_hid_publish(&m,1,true));
    assert(m.input.id.bustype==3 && (m.input.relbit[0]&3)==3);
    uint64_t total=0;
    assert(nas_x86_qemu_hid_poll(NULL,kbd,&k,4,&total,"bad")==2);
    assert(total==2 && rejected==1);
    assert(k.input.key_state[30/8]&(1U<<(30%8)));
    step=0;
    assert(nas_x86_qemu_hid_poll(NULL,mouse,&m,4,&total,"bad")==-EIO);
    assert(total==3 && (m.input.key_state[272/8]&1U));
    assert(!nas_x86_qemu_hid_unpublish(&m));
    assert(!nas_x86_qemu_hid_unpublish(&k));
    assert(!(k.input.key_state[30/8]&(1U<<(30%8))));
    assert(!(m.input.key_state[272/8]&1U));
    assert(!m.input.registered && !k.input.registered);
    puts("[QEMU HID SHARED HOST PASS] registration rollback, capabilities, poll errors and release");
    return 0;
}
