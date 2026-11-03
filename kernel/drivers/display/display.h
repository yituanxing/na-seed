#ifndef NASEED_DISPLAY_DEVICE_H
#define NASEED_DISPLAY_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

/* A scanout surface class, independent of firmware, PCI/MMIO transport,
 * userspace's Linux fbdev ABI and the desktop compositor. This first stage
 * deliberately has one stable primary device and one fixed mode: drivers
 * register before user files are opened and remain alive for the boot.
 * Mode setting, GPU command queues and runtime removal are not promised. */
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
};

struct nas_display_device {
    const char *name;                  /* Stable for the lifetime of the driver */
    void *backend;                    /* Private transport/memory ownership */
    struct nas_display_mode mode;
    const struct nas_display_ops *ops;
};

/* The first successfully registered scanout becomes /dev/fb0's backend.
 * Registration rejects a second primary rather than silently changing
 * memory while an OpenFile might still refer to the previous screen. */
bool nas_display_register_primary(struct nas_display_device *);
const struct nas_display_device *nas_display_primary(void);
uint64_t nas_display_length(void);
uint64_t nas_display_read_at(uint64_t,uint8_t *,uint64_t);
uint64_t nas_display_write_at(uint64_t,const uint8_t *,uint64_t);

#endif
