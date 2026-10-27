#ifndef NASEED_INIT_MAIN_H
#define NASEED_INIT_MAIN_H
#include <stdint.h>
void nas_init_userspace(void);
/* Called once when PID1 has no surviving workers and has finalized FDs/MM. */
void nas_init_process_terminated(uint32_t status,uint32_t signal);
#endif
