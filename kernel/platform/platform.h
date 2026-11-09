#ifndef NASEED_PLATFORM_H
#define NASEED_PLATFORM_H
void nas_platform_debug_exit(unsigned code);
/* Firmware/chipset reset after an original PID1 reboot(2) and durable unmount.
 * A successful platform reset never returns to the old kernel. */
void nas_platform_restart(void);
#endif
