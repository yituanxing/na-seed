#ifndef NASEED_INIT_MAIN_H
#define NASEED_INIT_MAIN_H
#include <stdint.h>
struct nas_task;
void nas_init_userspace(void);
/* Called once when PID1 has no surviving workers and has finalized FDs/MM. */
void nas_init_process_terminated(uint32_t status,uint32_t signal);
/* Original Alpine BusyBox init consumes masked shutdown signals and issues
 * reboot(2) from a vfork child. Real BusyBox PID1 then sleeps indefinitely;
 * scheduler-stack boot policy retires PID1 AFTER the child vfork release,
 * then drains ext4/workers through the established post-exit lifecycle. */
void nas_init_note_pid1_shutdown_consumed(unsigned signal);
uint64_t nas_init_alpine_power_request(const struct nas_task *caller,
                                       uint32_t command);
#endif
