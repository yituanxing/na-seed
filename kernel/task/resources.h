#ifndef NASEED_PROCESS_RESOURCES_H
#define NASEED_PROCESS_RESOURCES_H
#include <stdint.h>

/* Independently owned process resources, shared by explicit get/put.
 * Task leadership is never an allocation or lifetime owner. */
struct nas_fs_context {
    unsigned refs;
    uint32_t umask;
    char cwd[256];
};
struct nas_sighand {
    unsigned refs;
    uint64_t actions[65][4];
};
void nas_resources_system_init(void);
struct nas_fs_context *nas_fs_context_new(void);
struct nas_fs_context *nas_fs_context_copy(const struct nas_fs_context *);
struct nas_fs_context *nas_fs_context_get(struct nas_fs_context *);
void nas_fs_context_put(struct nas_fs_context *);
struct nas_sighand *nas_sighand_new(void);
struct nas_sighand *nas_sighand_copy(const struct nas_sighand *);
struct nas_sighand *nas_sighand_get(struct nas_sighand *);
void nas_sighand_put(struct nas_sighand *);
#endif
