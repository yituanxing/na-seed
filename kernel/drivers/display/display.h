#ifndef NASEED_DISPLAY_DEVICE_H
#define NASEED_DISPLAY_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

/* A scanout surface class, independent of firmware, PCI/MMIO transport,
 * userspace's Linux fbdev ABI and the desktop compositor. One fixed-mode
 * primary may be selected during bootstrap; a real driver may take over
 * Limine bootfb ONCE before user entry. A published primary is stable until
 * explicitly detached after all consumers release it. Modeset and GPU
 * commands live elsewhere. */
struct nas_display_mode {
    uint32_t width, height, pitch;
    uint16_t bpp;
    uint8_t red_size, red_shift;
    uint8_t green_size, green_shift;
    uint8_t blue_size, blue_shift;
};

struct nas_display_device;
struct nas_display_ops {
    /* Kernel-buffer-only byte transfer. A short transfer is observable;
     * callbacks must never access beyond the registered scanout length.
     * Drivers own synchronization, backing memory and all hardware state. */
    uint64_t (*read_at)(struct nas_display_device *,uint64_t,uint8_t *,uint64_t);
    uint64_t (*write_at)(struct nas_display_device *,uint64_t,
                         const uint8_t *,uint64_t);
    /* Optional deferred scanout commit. A framebuffer driver that writes
     * directly to visible firmware memory may leave this NULL. GPU drivers
     * can batch dirty byte writes and submit at a safe scheduler boundary;
     * this is NEVER called once for every internal 1-KiB FileOps chunk. */
    int (*present)(struct nas_display_device *);
};

struct nas_display_device {
    const char *name;                  /* Stable for the lifetime of the driver */
    void *backend;                    /* Private transport/memory ownership */
    struct nas_display_mode mode;
    const struct nas_display_ops *ops;
};

/* No display yet: register one. A registered non-firmware display cannot be
 * superseded. The distinct takeover path is for a genuine display device
 * replacing the provisional Limine bootfb exactly once before user entry. */
bool nas_display_register_primary(struct nas_display_device *);
bool nas_display_boot_takeover(struct nas_display_device *);
/* Detach exactly the registered owner. A published /dev/fb0 shared OpenFile
 * pins the scanout; removal fails -EBUSY without changing any class state.
 * Successful removal creates the only vacancy in which a locked class may
 * register another validated primary. Firmware takeover remains one-shot. */
int nas_display_unregister_primary(struct nas_display_device *);
/* Installed once by the fbdev character-device adapter after its stable
 * node is registered. The query counts live shared OpenFile descriptions,
 * not duplicated descriptors; a second different provider is rejected. */
int nas_display_set_open_pins_callback(unsigned (*query)(void));
/* Called immediately before entering userspace; late registration and all
 * replacements are then refused, including if no display was discovered.
 * A subsequent explicit unregistration opens a distinct rebind vacancy. */
void nas_display_lock_primary(void);
const struct nas_display_device *nas_display_primary(void);
uint64_t nas_display_length(void);
uint64_t nas_display_read_at(uint64_t,uint8_t *,uint64_t);
uint64_t nas_display_write_at(uint64_t,const uint8_t *,uint64_t);
/* Deferred present is explicit: a null backend hook is a successful no-op,
 * an absent display fails ENODEV, and errors propagate to the caller. */
int nas_display_present(void);

#endif
