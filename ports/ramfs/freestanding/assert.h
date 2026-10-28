#ifndef NASEED_RAMFS_PORT_ASSERT_H
#define NASEED_RAMFS_PORT_ASSERT_H
/* Fail closed: never compile upstream lifetime assertions into no-ops. */
#define assert(expr) do {if(!(expr))__builtin_trap();} while(0)
#endif
