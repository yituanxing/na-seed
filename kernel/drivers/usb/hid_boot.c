#include "hid_boot.h"
#include "../input/core.h"
#include "../../errno.h"

/* HID Usage Tables keyboard/keypad page (0x07) -> Linux EV_KEY code. The
 * numeric codes are Linux input-event-codes UAPI constants, not scancodes
 * or ASCII. Unknown usages produce no keyboard events. Boot-interface
 * modifiers E0..E7 are decoded exclusively from report byte 0. */
static const uint16_t boot_keycode[0x82U] = {
    /* Alphabet, HID 04..1D in alphabetical usage order. */
    [0x04]=30, [0x05]=48, [0x06]=46, [0x07]=32, [0x08]=18,
    [0x09]=33, [0x0a]=34, [0x0b]=35, [0x0c]=23, [0x0d]=36,
    [0x0e]=37, [0x0f]=38, [0x10]=50, [0x11]=49, [0x12]=24,
    [0x13]=25, [0x14]=16, [0x15]=19, [0x16]=31, [0x17]=20,
    [0x18]=22, [0x19]=47, [0x1a]=17, [0x1b]=45, [0x1c]=21,
    [0x1d]=44,
    /* 1..0, Enter, Esc, Backspace, Tab, Space and symbols. */
    [0x1e]=2, [0x1f]=3, [0x20]=4, [0x21]=5, [0x22]=6,
    [0x23]=7, [0x24]=8, [0x25]=9, [0x26]=10, [0x27]=11,
    [0x28]=28, [0x29]=1, [0x2a]=14, [0x2b]=15, [0x2c]=57,
    [0x2d]=12, [0x2e]=13, [0x2f]=26, [0x30]=27, [0x31]=43,
    [0x33]=39, [0x34]=40, [0x35]=41, [0x36]=51, [0x37]=52,
    [0x38]=53, [0x39]=58,
    /* F1-F12, print/scroll/pause, navigation and cursor. */
    [0x3a]=59, [0x3b]=60, [0x3c]=61, [0x3d]=62, [0x3e]=63,
    [0x3f]=64, [0x40]=65, [0x41]=66, [0x42]=67, [0x43]=68,
    [0x44]=87, [0x45]=88, [0x46]=99, [0x47]=70, [0x48]=119,
    [0x49]=110, [0x4a]=102, [0x4b]=104, [0x4c]=111,
    [0x4d]=107, [0x4e]=109, [0x4f]=106, [0x50]=105,
    [0x51]=108, [0x52]=103,
    /* Numeric keypad and international/common application keys. */
    [0x53]=69, [0x54]=98, [0x55]=55, [0x56]=74, [0x57]=78,
    [0x58]=96, [0x59]=79, [0x5a]=80, [0x5b]=81, [0x5c]=75,
    [0x5d]=76, [0x5e]=77, [0x5f]=71, [0x60]=72, [0x61]=73,
    [0x62]=82, [0x63]=83, [0x64]=86, [0x65]=127, [0x66]=116,
    [0x67]=117,
    [0x68]=183, [0x69]=184, [0x6a]=185, [0x6b]=186,
    [0x6c]=187, [0x6d]=188, [0x6e]=189, [0x6f]=190,
    [0x70]=191, [0x71]=192, [0x72]=193, [0x73]=194,
    /* Keyboard usage-page Mute/Volume keys. */
    [0x7f]=113, [0x80]=115, [0x81]=114
};
static const uint16_t modifier_keycode[8]={
    29,42,56,125,97,54,100,126
};

uint16_t nas_usb_hid_boot_keycode(uint8_t usage) {
    if(usage>=0xe0U && usage<=0xe7U)
        return modifier_keycode[usage-0xe0U];
    if(usage>=sizeof(boot_keycode)/sizeof(boot_keycode[0]))return 0;
    return boot_keycode[usage];
}

void nas_usb_hid_boot_init(struct nas_usb_hid_boot_keyboard *keyboard) {
    if(!keyboard)return;
    keyboard->previous_modifiers=0;
    for(unsigned i=0;i<NAS_USB_HID_BOOT_KEY_SLOTS;i++)
        keyboard->previous_keys[i]=0;
    keyboard->initialized=true;
}

static bool contains_usage(const uint8_t *slots,uint8_t usage) {
    for(unsigned i=0;i<NAS_USB_HID_BOOT_KEY_SLOTS;i++)
        if(slots[i]==usage)return true;
    return false;
}

int nas_usb_hid_boot_report(struct nas_usb_hid_boot_keyboard *keyboard,
                            const uint8_t *report,size_t bytes,
                            nas_usb_hid_boot_emit emit,void *context) {
    if(!keyboard || !keyboard->initialized || !report || !emit ||
       bytes!=NAS_USB_HID_BOOT_REPORT_BYTES)return -EINVAL;
    /* The H12 qualified QEMU Boot Keyboard fixture must zero the OEM
     * reserved byte. A nonzero value is outside our intentionally narrow
     * profile; preserve the previous key state without generating events. */
    if(report[1]!=0U)return -EPROTO;

    const uint8_t *incoming=report+2U;
    for(unsigned i=0;i<NAS_USB_HID_BOOT_KEY_SLOTS;i++) {
        const uint8_t usage=incoming[i];
        /* 01 ErrorRollOver, 02 POSTFail, 03 ErrorUndefined. No generated
         * releases: a held key must survive an in-flight rollover report. */
        if(usage>=1U && usage<=3U)return -EAGAIN;
        /* Modifiers must appear in byte 0, never key-array slots. */
        if(usage>=0xe0U && usage<=0xe7U)return -EPROTO;
        if(usage==0U)continue;
        for(unsigned j=0;j<i;j++)
            if(incoming[j]==usage)return -EPROTO;
    }

    bool changed=false;
    /* Releases first: key usages and then modifier bits. Logical order is
     * independent of the arbitrary ordering of the six USB key slots. */
    for(unsigned i=0;i<NAS_USB_HID_BOOT_KEY_SLOTS;i++) {
        const uint8_t usage=keyboard->previous_keys[i];
        if(!usage || contains_usage(incoming,usage))continue;
        const uint16_t code=nas_usb_hid_boot_keycode(usage);
        if(code) {
            emit(context,NAS_INPUT_EV_KEY,code,0);
            changed=true;
        }
    }
    const uint8_t removed=(uint8_t)(keyboard->previous_modifiers &
                                    (uint8_t)~report[0]);
    for(unsigned bit=0;bit<8;bit++)if(removed&(1U<<bit)) {
        emit(context,NAS_INPUT_EV_KEY,modifier_keycode[bit],0);
        changed=true;
    }

    /* Modifier presses precede ordinary key presses, so a fresh Shift+A
     * report reaches the tty/event handler in its intended state. */
    const uint8_t added=(uint8_t)(report[0] &
                                  (uint8_t)~keyboard->previous_modifiers);
    for(unsigned bit=0;bit<8;bit++)if(added&(1U<<bit)) {
        emit(context,NAS_INPUT_EV_KEY,modifier_keycode[bit],1);
        changed=true;
    }
    for(unsigned i=0;i<NAS_USB_HID_BOOT_KEY_SLOTS;i++) {
        const uint8_t usage=incoming[i];
        if(!usage || contains_usage(keyboard->previous_keys,usage))continue;
        const uint16_t code=nas_usb_hid_boot_keycode(usage);
        if(code) {
            emit(context,NAS_INPUT_EV_KEY,code,1);
            changed=true;
        }
    }
    keyboard->previous_modifiers=report[0];
    for(unsigned i=0;i<NAS_USB_HID_BOOT_KEY_SLOTS;i++)
        keyboard->previous_keys[i]=incoming[i];
    if(changed)emit(context,NAS_INPUT_EV_SYN,NAS_INPUT_SYN_REPORT,0);
    return 0;
}

void nas_usb_hid_boot_release_all(struct nas_usb_hid_boot_keyboard *keyboard,
                                  nas_usb_hid_boot_emit emit,void *context) {
    if(!keyboard || !keyboard->initialized || !emit)return;
    const uint8_t empty[NAS_USB_HID_BOOT_REPORT_BYTES]={0};
    (void)nas_usb_hid_boot_report(keyboard,empty,sizeof(empty),emit,context);
}
